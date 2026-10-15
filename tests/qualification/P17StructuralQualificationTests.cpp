#include "features/FeatureTestSupport.hpp"
#include "reference/StructuralTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <StructuralReferenceModels.hpp>

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/structural/StructuralCommands.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>
#include <bettercad/structural/StructuralResult.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// P17-QUAL-001 — the final cross-milestone gates.
//
// These are NOT re-runs of the per-milestone suites, which the three-preset
// regression re-runs in full anyway. Each one crosses milestone boundaries and
// is required by the qualification brief as a FINAL gate rather than as
// inherited evidence: a fact can hold in P17-DATA's tests, hold again in
// P17-CMD's and P17-PERSIST's, and still fail when the document, the
// parameters, the material, the history, the mesher and the solver are put in
// one sequence.
//
// The audit that preceded this file found that almost every gate the brief
// names is already covered, and names the covering test in
// docs/verification/P17-QUAL-001/MILESTONE_AUDIT.md. What is here is only what
// nothing covered:
//
//   the authority matrix, as one executable statement over EVERY reference
//   document -- solving must leave the canonical document byte-identical
//   every dependency edit in ONE sequence on one document, each followed by a
//   fresh solve, with undo in the middle
//   a regeneration that FAILS after a result was current, which no structural
//   test had put in sequence
namespace {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::structref;
using bettercad::reference::buildStructuralReferenceModel;
using bettercad::reference::kStructuralReferenceModels;
using bettercad::reference::StructuralReferenceModelInfo;
using bettercad::reference::StructuralReferenceModelKind;
using meshing::MeshCurrency;
using structural::AnalysisState;
using structural::ResultCurrency;
using structural::StaleReason;
using structural::StructuralSolveOutcome;

/// A reference model, built and regenerated.
[[nodiscard]] StructuralReference referenceModel(StructuralReferenceModelKind kind) {
    Result<Document> document = buildStructuralReferenceModel(kind);
    REQUIRE(document.has_value());
    return StructuralReference(std::move(*document));
}

/// The bytes a document saves to.
[[nodiscard]] std::string savedBytes(Document& document, const std::filesystem::path& path) {
    auto saved = io::saveDocument(document, path);
    INFO((saved.has_value() ? std::string{"ok"} : saved.error().message));
    REQUIRE(saved.has_value());
    return readFile(path);
}

/// The parameter named @p name, which the model must have.
[[nodiscard]] ParameterId parameterNamed(const Document& document, std::string_view name) {
    const std::optional<ObjectId> item = document.findByName(std::string{name});
    REQUIRE(item.has_value());
    const std::optional<ParameterId> parameter = document.asParameter(*item);
    REQUIRE(parameter.has_value());
    return *parameter;
}

/// The result's currency against the document AS IT IS NOW, through the
/// production entry -- never through a stored flag.
[[nodiscard]] ResultCurrency currencyOf(StructuralReference& model, const StructuralSolveOutcome& outcome) {
    const Result<structural::StructuralResultSource> current = structural::currentResultSource(
        model.document(), model.regenerator(), model.mesher(), model.analysis());
    if (!current) {
        // No current source can be computed, so nothing is current. The
        // analysis state says why; this helper only says "not current".
        return ResultCurrency::Stale;
    }
    return structural::resultCurrency(&outcome.result, *current);
}

[[nodiscard]] std::vector<StaleReason> staleReasonsOf(StructuralReference& model,
                                                      const StructuralSolveOutcome& outcome) {
    const Result<structural::StructuralResultSource> current = structural::currentResultSource(
        model.document(), model.regenerator(), model.mesher(), model.analysis());
    REQUIRE(current.has_value());
    return structural::staleReasons(outcome.result.source(), *current);
}

[[nodiscard]] AnalysisState stateOf(StructuralReference& model, const StructuralSolveOutcome* last) {
    return structural::analysisState(model.document(), model.regenerator(), model.mesher(),
                                     model.analysis(), last != nullptr ? &last->result : nullptr,
                                     nullptr);
}

/// A solve on the mesh ALREADY HELD, with no regeneration: the production
/// entry refuses if that mesh is not current, so this is also the statement
/// that the edit before it left the mesh alone. `requireSolve` regenerates,
/// which moves the mesh stamp and would make every earlier result stale for
/// the mesh's reason as well as the edit's.
[[nodiscard]] StructuralSolveOutcome solveHeld(StructuralReference& model) {
    Result<StructuralSolveOutcome> outcome = model.solve();
    if (!outcome) {
        FAIL("solve refused: " << outcome.error().message);
    }
    return std::move(*outcome);
}

/// The document-level material, which every reference model assigns.
[[nodiscard]] MaterialId assignedMaterial(const Document& document) {
    const features::MaterialAssignment assignment = features::materialAssignment(document);
    REQUIRE(assignment.resolved());
    REQUIRE(assignment.material.has_value());
    return *assignment.material;
}

} // namespace

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

TEST_CASE("P17Qualification_SolvingLeavesEveryReferenceDocumentByteIdentical",
          "[p17qual][authority][persist]") {
    // THE AUTHORITY MATRIX, AS ONE EXECUTABLE STATEMENT, OVER EVERY MODEL.
    //
    // ADR-045 makes the solved result derived state held beside the mesh,
    // never a document object; the brief's authority table says CAD geometry,
    // the P15 material, the P16 meshing intent and the structural intent are
    // canonical and that nodes, DOF numbering, stiffness, displacements,
    // strains, stresses and reactions are not.
    //
    // Every one of those is a claim about what the DOCUMENT contains, so the
    // sharpest check is the document itself: save it, mesh and solve it, save
    // it again, and require the bytes to be identical. P17-PERSIST-001 proves
    // this on one hand-built part in four states; the FINAL gate needs it on
    // every committed reference document, including the two whose solve is
    // REFUSED -- a refusal that wrote a diagnostic or a partial result into
    // the document would change the bytes just as a result would.
    TempDir directory;
    std::size_t solved = 0;
    std::size_t refused = 0;

    for (const StructuralReferenceModelInfo& info : kStructuralReferenceModels) {
        INFO(info.id << " " << info.name);
        StructuralReference model = referenceModel(info.kind);
        const std::filesystem::path before = directory.path() / std::format("{}-before.bcad", info.fileStem);
        const std::filesystem::path after = directory.path() / std::format("{}-after.bcad", info.fileStem);

        const std::string bytesBefore = savedBytes(model.document(), before);
        const std::size_t objectsBefore = model.document().objects().size();
        const std::uint64_t revisionBefore = model.document().revision();

        model.require();
        const Result<StructuralSolveOutcome> outcome = model.solve();
        REQUIRE(outcome.has_value() == info.expectSolve);
        if (outcome) {
            // Not vacuous: a solve really happened and really produced numbers.
            CHECK(outcome->result.displacements().size() == model.mesher().mesh(model.control())->nodeCount());
            CHECK(outcome->largestDisplacement.si() > 0.0);
            CHECK(currencyOf(model, *outcome) == ResultCurrency::Current);
            ++solved;
        } else {
            CHECK_FALSE(outcome.error().message.empty());
            ++refused;
        }

        const std::string bytesAfter = savedBytes(model.document(), after);
        INFO(std::format("{} bytes before, {} after", bytesBefore.size(), bytesAfter.size()));
        CHECK(bytesAfter == bytesBefore);
        // And nothing was added to the document, nor was its revision moved:
        // meshing is not an edit and neither is solving, nor a refusal.
        CHECK(model.document().objects().size() == objectsBefore);
        CHECK(model.document().revision() == revisionBefore);
        // No derived array is in the file under any of its names.
        CHECK(bytesAfter.find("\"displacements\"") == std::string::npos);
        CHECK(bytesAfter.find("\"stresses\"") == std::string::npos);
        CHECK(bytesAfter.find("\"strains\"") == std::string::npos);
        CHECK(bytesAfter.find("\"reactions\"") == std::string::npos);
        CHECK(bytesAfter.find("\"stiffness\"") == std::string::npos);
        CHECK(bytesAfter.find("\"nodes\"") == std::string::npos);
        CHECK(bytesAfter.find("\"tetrahedra\"") == std::string::npos);
    }

    CHECK(solved + refused == kStructuralReferenceModels.size());
    CHECK(solved == 10);
    CHECK(refused == 2);
    WARN(std::format("{} reference documents saved byte-identically before and after the production "
                     "entry: {} solved, {} refused",
                     solved + refused, solved, refused));
}

TEST_CASE("P17Qualification_EveryDependencyEditStalesTheResultInOneSequence",
          "[p17qual][currentness][command]") {
    // THE CURRENTNESS MATRIX, AS ONE SEQUENCE ON ONE DOCUMENT.
    //
    // P17-DATA-001 proves each row of the truth table against a mutated
    // source struct and the material and geometry rows against a document;
    // P17-CMD-001 proves that every physics command stales a result and that
    // undo does not resurrect one. What no test did is run the rows one after
    // another on the same document, with a FRESH SOLVE after each, so that a
    // result left current by one row and stale by the next is seen by the
    // next row's check rather than by its own. The brief asks "can a material
    // edit leave an old result current?", "a load edit?", "a restraint
    // edit?" -- the same question six times, and here it is asked six times
    // in a row.
    //
    // RM-FEA-04, the pressure block: eight nodes, six tetrahedra, so the six
    // solves cost nothing, and a FIXED support plus a pressure give a result
    // that a modulus, a magnitude and a second restraint all genuinely change.
    // Built through the model's own builder rather than the catalog, so the
    // side-face names are in hand for step 5; the document is the same one.
    Result<bettercad::reference::StructuralPressureBlockModel> shape =
        bettercad::reference::buildStructuralPressureBlockReferenceModel();
    REQUIRE(shape.has_value());
    const FaceName side = shape->side(0);
    StructuralReference model(std::move(shape->document));
    CommandHistory history;
    const MaterialId material = assignedMaterial(model.document());

    StructuralSolveOutcome current = model.requireSolve();
    REQUIRE(currencyOf(model, current) == ResultCurrency::Current);
    REQUIRE(stateOf(model, &current) == AnalysisState::SolvedCurrent);
    const std::uint64_t original = fingerprintOf(current);

    // 1. THE GEOMETRY. The input boundary refuses before a source can be
    //    computed, the mesh is stale through P16's own currency, and after a
    //    remesh the old result names both a moved geometry and a moved mesh.
    const ParameterId width = parameterNamed(model.document(), "press_a");
    REQUIRE(model.document().setParameterValue(width, 72.0 * units::mm).has_value());
    CHECK(model.meshed().regenerate().succeeded());
    CHECK(model.meshed().currency() == MeshCurrency::StaleGeometry);
    CHECK(stateOf(model, &current) == AnalysisState::InputsUnavailable);
    CHECK(currencyOf(model, current) != ResultCurrency::Current);
    model.require();
    CHECK(model.meshed().currency() == MeshCurrency::Current);
    CHECK(currencyOf(model, current) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, current) == std::vector{StaleReason::Geometry, StaleReason::Mesh});
    StructuralSolveOutcome afterGeometry = model.requireSolve();
    CHECK(currencyOf(model, afterGeometry) == ResultCurrency::Current);
    CHECK(currencyOf(model, current) == ResultCurrency::Stale);
    CHECK(fingerprintOf(afterGeometry) != original);

    // 2. THE MESHING INTENT. The mesh goes stale by intent, the result with it;
    //    nothing in the structural definition moved.
    meshing::MeshControlDefinition intent = model.meshed().definition()->definition();
    intent.mesh.sizing.globalTargetSize = 15_mm;
    model.setMeshDefinition(intent);
    CHECK(model.meshed().currency() == MeshCurrency::StaleIntent);
    CHECK(stateOf(model, &afterGeometry) == AnalysisState::InputsUnavailable);
    model.require();
    CHECK(currencyOf(model, afterGeometry) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, afterGeometry) == std::vector{StaleReason::Mesh});
    StructuralSolveOutcome afterIntent = model.requireSolve();
    CHECK(currencyOf(model, afterIntent) == ResultCurrency::Current);

    // 3. THE MATERIAL. A modulus edit leaves the mesh CURRENT -- E is in
    //    neither the geometry nor the meshing intent -- and stales the result
    //    for exactly one reason.
    materials::MechanicalProperties mechanical =
        features::findMaterial(model.document(), material)->definition().mechanical;
    mechanical.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(190_GPa);
    REQUIRE(features::setMaterialMechanical(model.document(), material, mechanical).has_value());
    CHECK(model.meshed().currency() == MeshCurrency::Current);
    CHECK(currencyOf(model, afterIntent) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, afterIntent) == std::vector{StaleReason::Material});
    CHECK(stateOf(model, &afterIntent) == AnalysisState::SolvedStale);
    StructuralSolveOutcome afterMaterial = solveHeld(model);
    CHECK(currencyOf(model, afterMaterial) == ResultCurrency::Current);
    // A softer steel moves more under the same pressure, so this is a
    // different answer and not merely a re-stamped one.
    CHECK(afterMaterial.largestDisplacement.si() > afterIntent.largestDisplacement.si());
    CHECK(fingerprintOf(afterMaterial) != fingerprintOf(afterIntent));

    // 4. A LOAD, through the command layer and the real history.
    const structural::StructuralLoad load = model.definition().loads.front();
    const structural::PressureLoad* pressure = load.pressure();
    REQUIRE(pressure != nullptr);
    structural::PressureLoad doubled = *pressure;
    doubled.magnitude = Pressure::fromSi(2.0 * pressure->magnitude.si());
    REQUIRE(history
                .execute(model.document(), std::make_unique<structural::EditStructuralLoadCommand>(
                                               model.analysis(),
                                               structural::StructuralLoad{load.id(), doubled}))
                .has_value());
    CHECK(model.meshed().currency() == MeshCurrency::Current);
    CHECK(currencyOf(model, afterMaterial) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, afterMaterial) == std::vector{StaleReason::Analysis});
    StructuralSolveOutcome afterLoad = solveHeld(model);
    CHECK(currencyOf(model, afterLoad) == ResultCurrency::Current);
    // Linear: twice the pressure is twice the displacement, to roundoff.
    CHECK(std::abs(afterLoad.largestDisplacement.si() - 2.0 * afterMaterial.largestDisplacement.si()) <=
          1e-12 * afterLoad.largestDisplacement.si());

    // 5. A RESTRAINT, likewise. Holding one side face along x stops the
    //    Poisson expansion on that side, so the constrained set and the
    //    answer both change and the mesh does not.
    REQUIRE(history
                .execute(model.document(),
                         std::make_unique<structural::AddStructuralRestraintCommand>(
                             model.analysis(),
                             structural::StructuralRestraint{
                                 RestraintId::fromValue(77U), side,
                                 structural::RestraintComponents::along(structural::DofComponent::Ux)}))
                .has_value());
    CHECK(model.meshed().currency() == MeshCurrency::Current);
    CHECK(currencyOf(model, afterLoad) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, afterLoad) == std::vector{StaleReason::Analysis});
    StructuralSolveOutcome afterRestraint = solveHeld(model);
    CHECK(currencyOf(model, afterRestraint) == ResultCurrency::Current);
    CHECK(afterRestraint.validation.constrainedDegreesOfFreedom > afterLoad.validation.constrainedDegreesOfFreedom);
    CHECK(fingerprintOf(afterRestraint) != fingerprintOf(afterLoad));
    CHECK(currencyOf(model, afterLoad) == ResultCurrency::Stale);

    // 6. UNDO. The content is back, bit for bit; the result computed under it
    //    is STILL NOT CURRENT, because the revision moved forward and not
    //    back; and the fresh solve agrees with the one made before the
    //    restraint was added, to the last bit -- it is recomputed from the
    //    intent, not revived from history.
    REQUIRE(history.undo(model.document()).has_value());
    CHECK(model.definition().restraints.size() == 1U);
    CHECK(currencyOf(model, afterLoad) == ResultCurrency::Stale);
    CHECK(currencyOf(model, afterRestraint) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, afterLoad) == std::vector{StaleReason::Analysis});
    StructuralSolveOutcome afterUndo = solveHeld(model);
    CHECK(currencyOf(model, afterUndo) == ResultCurrency::Current);
    CHECK(fingerprintOf(afterUndo) == fingerprintOf(afterLoad));

    // 7. THE SOLVER SETTINGS. Canonical since ADR-047, so an edit stales the
    //    result -- and the fresh solve gives the SAME numbers, because a
    //    tolerance is an acceptance threshold and not an input to the
    //    arithmetic.
    structural::SolverSettings settings = model.definition().solver;
    settings.relativeResidualTolerance = 0.5 * settings.relativeResidualTolerance;
    REQUIRE(history
                .execute(model.document(), std::make_unique<structural::SetSolverSettingsCommand>(
                                               model.analysis(), settings))
                .has_value());
    CHECK(model.meshed().currency() == MeshCurrency::Current);
    CHECK(currencyOf(model, afterUndo) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, afterUndo) == std::vector{StaleReason::Analysis});
    StructuralSolveOutcome afterSettings = solveHeld(model);
    CHECK(currencyOf(model, afterSettings) == ResultCurrency::Current);
    CHECK(fingerprintOf(afterSettings) == fingerprintOf(afterUndo));

    // Throughout: the first result never came back, and the mesh held since
    // step 2 is the one every later solve used -- five solves on one mesh
    // stamp, so every stale verdict above was the EDIT's and not a remesh's.
    CHECK(currencyOf(model, current) == ResultCurrency::Stale);
    CHECK(afterSettings.result.source().mesh == afterIntent.result.source().mesh);
    WARN(std::format("RM-FEA-04 in one sequence: geometry, meshing intent, material, load, restraint, "
                     "undo, solver settings -- seven edits, seven stale verdicts, eight solves, the "
                     "first result stale at the end (fingerprints 0x{:x} -> 0x{:x})",
                     original, fingerprintOf(afterSettings)));
}

TEST_CASE("P17Qualification_AFailedRegenerationNeverYieldsACurrentResult",
          "[p17qual][stale][currentness]") {
    // THE WHOLE STALE-GEOMETRY CONTRACT IN ONE SEQUENCE, including the case
    // that matters most and is hardest to reach: a regeneration that FAILS
    // after a result was already current.
    //
    // P16-QUAL-001 proved this for the MESH. P17's input boundary inherits
    // every geometry gate through requireStructuralModel, and RM-FEA-09 proves
    // the stale-mesh refusal, but no structural test put a current result, an
    // edit, a FAILED rebuild and a recovery in one order. At a final phase
    // gate, a requirement met by inheritance is not met.
    StructuralReference model = referenceModel(StructuralReferenceModelKind::StaleMesh);
    const StructuralSolveOutcome first = model.requireSolve();
    REQUIRE(currencyOf(model, first) == ResultCurrency::Current);
    REQUIRE(stateOf(model, &first) == AnalysisState::SolvedCurrent);
    const std::uint64_t original = fingerprintOf(first);

    // 1. AN EDIT THAT MAKES THE BODY IMPOSSIBLE. A zero depth cannot be
    //    extruded, so the rebuild fails and there is no last-known-good
    //    geometry to be tempted by.
    const ParameterId depth = parameterNamed(model.document(), "stale_c");
    REQUIRE(model.document().setParameterValue(depth, 0.0 * units::mm).has_value());
    const features::RegenerationReport failed = model.meshed().regenerate();
    CHECK_FALSE(failed.succeeded());

    // 2. NOTHING DOWNSTREAM IS CURRENT, and every layer says so in its own
    //    words rather than inferring it from the one above.
    CHECK_FALSE(meshing::describesTheModel(model.meshed().currency()));
    const structural::StructuralValidationReport report = model.validate();
    CHECK(report.status() == structural::ValidationStatus::Rejected);
    CHECK(report.reachedStage == structural::StructuralValidationReport::Stage::Inputs);
    REQUIRE(report.inputProblem.has_value());
    INFO("input problem: " << structural::toString(*report.inputProblem));
    CHECK(*report.inputProblem == structural::InputProblem::GeometryIneligible);
    // Nothing was measured past the boundary: no nodes, no DOFs, no loads.
    CHECK(report.nodeCount == 0);
    CHECK(report.constrainedDegreesOfFreedom == 0);
    CHECK(report.loadedNodes == 0);
    // The production entry refuses outright, and the mesher still holds the
    // old mesh -- stale is old, not deleted -- without it describing anything.
    const Result<StructuralSolveOutcome> refused = model.solve();
    REQUIRE_FALSE(refused.has_value());
    CHECK(model.mesher().mesh(model.control()) != nullptr);
    CHECK(stateOf(model, &first) == AnalysisState::InputsUnavailable);
    CHECK(currencyOf(model, first) != ResultCurrency::Current);
    // And generating a mesh is refused too, so no path re-validates the old
    // body on the way to a solve.
    CHECK_FALSE(model.meshed().generate().has_value());

    // 3. A GOOD VALUE RECOVERS -- the ORIGINAL value, so the recovered answer
    //    must agree with the first one to the last bit. A recovery that
    //    "mostly" agreed would mean the failed rebuild had left something
    //    behind.
    REQUIRE(model.document().setParameterValue(depth, 30.0 * units::mm).has_value());
    CHECK(model.meshed().regenerate().succeeded());
    const StructuralSolveOutcome recovered = model.requireSolve();
    CHECK(currencyOf(model, recovered) == ResultCurrency::Current);
    CHECK(stateOf(model, &recovered) == AnalysisState::SolvedCurrent);
    CHECK(fingerprintOf(recovered) == original);
    // The FIRST result is still stale: its geometry and mesh revisions are
    // behind, however the numbers compare.
    CHECK(currencyOf(model, first) == ResultCurrency::Stale);
    CHECK(staleReasonsOf(model, first) == std::vector{StaleReason::Geometry, StaleReason::Mesh});
    WARN(std::format("RM-FEA-09 failed-rebuild sequence: solved (0x{:x}), depth 0 refused at the "
                     "inputs stage with {}, depth 30 recovered to the same fingerprint, the first "
                     "result stale",
                     original, structural::toString(*report.inputProblem)));
}

#endif // BETTERCAD_TESTS_EXPECT_NETGEN
