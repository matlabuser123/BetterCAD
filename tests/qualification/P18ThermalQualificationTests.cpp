// P18-QUAL-001 — the final cross-milestone gates.
//
// These are NOT re-runs of the per-milestone suites, which the three-preset regression runs in full anyway. Each one crosses
// milestone boundaries and is required by the qualification brief as a FINAL gate rather than as inherited evidence: a fact can
// hold in P18-DATA's tests, hold again in P18-CMD's and P18-PERSIST's, and still fail when the document, the parameters, the
// material, the history, the mesher and the whole chain are put in one sequence.
//
// The audit that preceded this file mapped every requirement of the brief to a covering test
// (docs/verification/P18-QUAL-001/REQUIREMENT_MAP.md). What is here is only what nothing covered in this form:
//
//   the authority matrix, as one executable statement over EVERY committed thermal reference document -- running the whole chain
//   (regenerate, mesh, load/BC preparation, assembly, solve, recovery, balance, validation) must leave the canonical document
//   byte-identical, the object count and the document revision unmoved
//
//   the currentness matrix: each dependency edit in ONE sequence on one document, each followed by a fresh run, with undo in the
//   middle, reporting WHICH derived objects (DOF map, prepared loads, prepared boundary, system, solution, recovered fields,
//   balance) each edit staled
//
//   a regeneration that FAILS after a result was current, which no thermal test had put in sequence
#include "reference/ThermalReferenceSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/Command.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/thermal/ThermalCommands.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::thermalref;
using thermal::ValidationSubject;

[[nodiscard]] std::string savedBytes(const Document& document, const std::filesystem::path& path) {
    const auto saved = io::saveDocument(document, path);
    INFO((saved.has_value() ? std::string{"ok"} : saved.error().message));
    REQUIRE(saved.has_value());
    return readFile(path);
}

/// The derived objects a validation report calls stale.
[[nodiscard]] std::set<ValidationSubject> staleSubjects(const thermal::ThermalValidationReport& report) {
    std::set<ValidationSubject> out;
    for (const thermal::ValidationFinding& f : report.findings) {
        if (f.code == thermal::ValidationCode::SourceStale && f.subject.has_value()) {
            out.insert(*f.subject);
        }
    }
    return out;
}

/// An older run judged against the document AS IT IS NOW, through the production validator -- never through a stored flag.
[[nodiscard]] thermal::ThermalValidationReport judge(Run& run, const thermal::ThermalAnalysisRun& older) {
    return thermal::validateThermalAnalysis(run.document(), run.ref().regenerator(), run.ref().mesher(), run.analysis(),
                                            older.derivedState());
}

/// A bitwise hash of every nodal temperature of every stored state: "the same answer" means the same bits.
[[nodiscard]] std::uint64_t fingerprintOf(const thermal::ThermalAnalysisRun& run) {
    REQUIRE(run.post.has_value());
    std::uint64_t h = 14695981039346656037ULL;
    for (const thermal::ThermalPostState& state : run.post->states()) {
        for (const Temperature& t : state.temperatures) {
            h ^= std::bit_cast<std::uint64_t>(t.si());
            h *= 1099511628211ULL;
        }
    }
    return h;
}

[[nodiscard]] thermal::ThermalAnalysisDefinition definitionOf(Run& run) {
    const thermal::ThermalAnalysis* object = thermal::findThermalAnalysis(run.document(), run.analysis());
    REQUIRE(object != nullptr);
    return object->definition();
}

template <typename Command, typename... Arguments>
void execute(Run& run, CommandHistory& history, Arguments&&... arguments) {
    const auto done = history.execute(run.document(), std::make_unique<Command>(std::forward<Arguments>(arguments)...));
    INFO((done.has_value() ? std::string{"ok"} : done.error().message));
    REQUIRE(done.has_value());
}

[[nodiscard]] MaterialId assignedMaterial(const Document& document) {
    const features::MaterialAssignment assignment = features::materialAssignment(document);
    REQUIRE(assignment.resolved());
    REQUIRE(assignment.material.has_value());
    return *assignment.material;
}

const std::set<ValidationSubject> kEverything{ValidationSubject::DofMap,     ValidationSubject::PreparedLoads,
                                              ValidationSubject::PreparedBoundary, ValidationSubject::System,
                                              ValidationSubject::Solution,   ValidationSubject::PostResult,
                                              ValidationSubject::BalanceResult};
/// What is downstream of the system: the solution and everything recovered from it.
const std::set<ValidationSubject> kFromTheSolution{ValidationSubject::Solution, ValidationSubject::PostResult,
                                                    ValidationSubject::BalanceResult};

[[nodiscard]] std::set<ValidationSubject> plus(std::set<ValidationSubject> base, std::initializer_list<ValidationSubject> more) {
    base.insert(more.begin(), more.end());
    return base;
}

} // namespace

TEST_CASE("P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical", "[p18qual][authority][persist]") {
    // THE AUTHORITY MATRIX, AS ONE EXECUTABLE STATEMENT, OVER EVERY MODEL.
    //
    // The brief's authority table says CAD geometry, the P15 material, the P16 meshing intent and the thermal intent (analysis
    // definition, loads, conditions, initial condition, settings) are canonical, and that the mesh, the DOF numbering, K, C, Q, the
    // temperatures, gradients, fluxes, reactions, the balance, the validation report and the VIZ buffers are not. Every one of those
    // is a claim about what the DOCUMENT contains, so the sharpest check is the document itself: save it BEFORE anything is
    // regenerated, run the whole production chain (including the refused ones, whose refusal must not write a diagnostic or a partial
    // result into the document either), save it again, and require the bytes to be identical.
    TempDir directory;
    std::size_t accepted = 0;
    std::size_t refused = 0;

    for (const reference::ThermalReferenceModelInfo& info : reference::kThermalReferenceModels) {
        INFO(info.id << " " << info.name);
        Result<Document> built = reference::buildThermalReferenceModel(info.kind);
        REQUIRE(built.has_value());
        const std::filesystem::path before = directory.path() / std::format("{}-before.bcad", info.fileStem);
        const std::filesystem::path after = directory.path() / std::format("{}-after.bcad", info.fileStem);
        const std::filesystem::path again = directory.path() / std::format("{}-again.bcad", info.fileStem);

        const std::string bytesBefore = savedBytes(*built, before);
        const std::size_t objectsBefore = built->objects().size();
        const std::uint64_t revisionBefore = built->revision();

        Run run(std::move(*built)); // regenerates, meshes, runs the chain and judges it
        CHECK(run.run().accepted() == info.expectAccepted);
        if (run.run().accepted()) {
            // Not vacuous: a solution, recovered fields and a balance really exist, with a real number in them.
            REQUIRE(run.run().post.has_value());
            REQUIRE(run.run().balance.has_value());
            CHECK(run.post().finalState().temperatures.size() == run.mesh().nodes().size());
            ++accepted;
        } else {
            CHECK_FALSE(run.run().post.has_value());
            CHECK_FALSE(run.run().balance.has_value());
            CHECK_FALSE(run.run().steady.has_value());
            CHECK_FALSE(run.run().transient.has_value());
            ++refused;
        }

        const std::string bytesAfter = savedBytes(run.document(), after);
        INFO(std::format("{} bytes before, {} after", bytesBefore.size(), bytesAfter.size()));
        CHECK(bytesAfter == bytesBefore);
        CHECK(run.document().objects().size() == objectsBefore);
        CHECK(run.document().revision() == revisionBefore);

        // a SECOND run, and the file after it
        run.rerun();
        CHECK(run.run().accepted() == info.expectAccepted);
        CHECK(savedBytes(run.document(), again) == bytesBefore);

        // No derived array is in the file under any of its names.
        for (const char* word : {"\"temperatures\"", "\"gradients\"", "\"heat_fluxes\"", "\"reactions\"", "\"balance\"", "\"nodes\"",
                                 "\"tetrahedra\"", "\"conductance\"", "\"capacity\"", "\"validation\""}) {
            INFO("the saved file mentions " << word);
            CHECK(bytesAfter.find(word) == std::string::npos);
        }
    }

    CHECK(accepted + refused == reference::kThermalReferenceModels.size());
    CHECK(accepted == 15);
    CHECK(refused == 5);
    WARN(std::format("{} thermal reference documents saved byte-identically before and after the production chain: {} accepted, {} refused",
                     accepted + refused, accepted, refused));
}

TEST_CASE("P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches", "[p18qual][currentness][command]") {
    // THE CURRENTNESS MATRIX, AS ONE SEQUENCE ON ONE DOCUMENT.
    //
    // Each per-milestone suite proves its own row of the truth table against a mutated source. What none did is run the rows one
    // after another on the same document, with a FRESH RUN after each, and ask the validator WHICH derived objects each edit staled:
    // the DOF map, the prepared loads, the prepared boundary, the assembled system, the solution, the recovered fields, the balance.
    // The brief asks "can a material edit leave an old result current? a load edit? a boundary-condition edit?" -- the same question
    // for every edit, and here it is asked in a row.
    //
    // ThermalSurfaceFlux: a held cap and a surface flux through the other, so a modulus, a flux, a temperature and the settings all
    // genuinely change the answer or the verdict.
    auto built = reference::buildThermalSurfaceFluxReferenceModel();
    REQUIRE(built.has_value());
    Run run(std::move(built->document));
    CommandHistory history;
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r0 = run.takeRun();
    const std::uint64_t original = fingerprintOf(*r0);
    CHECK(judge(run, *r0).accepted()); // current before the first edit
    CHECK(staleSubjects(judge(run, *r0)).empty());

    // 1. THE MESH ALONE. Same body, same intent, a NEW mesh object: the numbering, the preparations, the system, the solution, the
    //    fields and the balance all name the old one. (The geometry did not move; only the mesh's own identity differs.)
    run.remeshOnly();
    {
        const thermal::ThermalValidationReport report = judge(run, *r0);
        CHECK_FALSE(report.accepted());
        CHECK(staleSubjects(report) == kEverything);
    }
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run.takeRun();
    CHECK(fingerprintOf(*r1) == original); // the same physics on the new mesh object, bit for bit (a deterministic mesher)
    CHECK(judge(run, *r1).accepted());

    // 2. THE MATERIAL: the conductivity. The mesh stays CURRENT, the numbering and the preparations do not depend on k, and
    //    everything from the system on does.
    const MaterialId material = assignedMaterial(run.document());
    {
        materials::ThermalProperties thermalProps = features::findMaterial(run.document(), material)->definition().thermal;
        thermalProps.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(8.0));
        REQUIRE(features::setMaterialThermal(run.document(), material, thermalProps).has_value());
    }
    CHECK(run.ref().currency() == meshing::MeshCurrency::Current);
    {
        const thermal::ThermalValidationReport report = judge(run, *r1);
        CHECK_FALSE(report.accepted());
        CHECK(staleSubjects(report) == plus(kFromTheSolution, {ValidationSubject::System}));
    }
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r2 = run.takeRun();
    CHECK(judge(run, *r2).accepted());
    CHECK(fingerprintOf(*r2) != fingerprintOf(*r1)); // a different k is a different answer, not a re-stamped one
    CHECK(judge(run, *r1).accepted() == false);

    // 3. A LOAD, through the command layer and the real history. The prepared loads, the system and everything downstream are
    //    stale; the numbering and the prepared boundary are not.
    {
        const thermal::ThermalLoad load = definitionOf(run).loads.front();
        const thermal::SurfaceHeatFluxLoad* flux = load.surfaceHeatFlux();
        REQUIRE(flux != nullptr);
        execute<thermal::EditThermalLoadCommand>(
            run, history, run.analysis(),
            thermal::ThermalLoad{load.id(), thermal::SurfaceHeatFluxLoad{.face = flux->face, .flux = HeatFlux::fromSi(1.2 * flux->flux.si())}});
    }
    CHECK(run.ref().currency() == meshing::MeshCurrency::Current);
    CHECK(staleSubjects(judge(run, *r2)) == plus(kFromTheSolution, {ValidationSubject::System, ValidationSubject::PreparedLoads}));
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r3 = run.takeRun();
    CHECK(judge(run, *r3).accepted());
    CHECK(fingerprintOf(*r3) != fingerprintOf(*r2));

    // 4. A PRESCRIBED TEMPERATURE. The prepared boundary, the solution and what hangs off it are stale -- and the assembled SYSTEM IS NOT:
    //    base assembly does no Dirichlet elimination (ADR-049), so K and Q do not depend on a held value; the constraint map and the
    //    solve do. (A convection edit, which enters K and Q, stales the system: see the transient case.)
    {
        const thermal::ThermalBoundaryCondition condition = definitionOf(run).boundaryConditions.front();
        const thermal::PrescribedTemperature* held = condition.prescribedTemperature();
        REQUIRE(held != nullptr);
        execute<thermal::EditThermalBoundaryConditionCommand>(
            run, history, run.analysis(),
            thermal::ThermalBoundaryCondition{condition.id(), thermal::PrescribedTemperature{.face = held->face, .value = Temperature::fromSi(310.0)}});
    }
    CHECK(run.ref().currency() == meshing::MeshCurrency::Current);
    CHECK(staleSubjects(judge(run, *r3)) == plus(kFromTheSolution, {ValidationSubject::PreparedBoundary}));
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r4 = run.takeRun();
    CHECK(judge(run, *r4).accepted());
    CHECK(fingerprintOf(*r4) != fingerprintOf(*r3));

    // 5. UNDO of the prescribed temperature -- THE TWO CURRENTNESS REGIMES (ADR-055).
    //    The numerical objects (preparations, system, solution, recovered fields, balance) are keyed by the CONTENT of the inputs they
    //    were computed from, so once undo has put the identical content back, r3 describes the document again: not a guess, because a
    //    fresh run of this very source produces the same arrays bit for bit (asserted). The VALIDATION REPORT is keyed by the analysis
    //    object's REVISION, which an undo moves forward and never back, so r3's report stays stale and never revives: nothing is
    //    ACCEPTED on the strength of an old verdict. What is never allowed is the reverse -- r4, computed for the EDITED content, must
    //    not be current under the restored one, whatever its numbers.
    REQUIRE(history.undo(run.document()).has_value());
    CHECK(staleSubjects(judge(run, *r3)).empty());
    CHECK(staleSubjects(judge(run, *r4)) == plus(kFromTheSolution, {ValidationSubject::PreparedBoundary}));
    CHECK_FALSE(judge(run, *r4).accepted());
    CHECK_FALSE(thermal::isCurrent(r3->validation, run.document(), run.ref().regenerator(), run.ref().mesher())); // revision-keyed
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r5 = run.takeRun();
    CHECK(fingerprintOf(*r5) == fingerprintOf(*r3)); // recomputed from the intent, bit for bit what r3 holds
    CHECK(thermal::isCurrent(r5->validation, run.document(), run.ref().regenerator(), run.ref().mesher()));
    // 6. THE SOLVER SETTINGS. Canonical, so an edit stales the solution and what hangs off it -- and the fresh run gives the SAME
    //    numbers, because a tolerance is an acceptance threshold and not an input to the arithmetic.
    {
        thermal::ThermalSolverSettings settings = definitionOf(run).solver;
        settings.relativeResidualTolerance = 0.5 * settings.relativeResidualTolerance;
        execute<thermal::SetThermalSolverSettingsCommand>(run, history, run.analysis(), settings);
    }
    {
        const std::set<ValidationSubject> staled = staleSubjects(judge(run, *r5));
        INFO("a solver-settings edit staled " << staled.size() << " objects");
        CHECK(staled.contains(ValidationSubject::Solution));
        CHECK(staled.contains(ValidationSubject::PostResult));
        CHECK(staled.contains(ValidationSubject::BalanceResult));
        CHECK_FALSE(staled.contains(ValidationSubject::DofMap));
        CHECK_FALSE(staled.contains(ValidationSubject::PreparedLoads));
        CHECK_FALSE(staled.contains(ValidationSubject::PreparedBoundary));
        CHECK_FALSE(staled.contains(ValidationSubject::System));
    }
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r6 = run.takeRun();
    CHECK(fingerprintOf(*r6) == fingerprintOf(*r5));
    CHECK(judge(run, *r6).accepted());

    // Throughout: the first result never came back, however many times the document returned to numbers it once had.
    CHECK(judge(run, *r0).accepted() == false);
    CHECK(judge(run, *r1).accepted() == false);
    CHECK_FALSE(thermal::isCurrent(r0->validation, run.document(), run.ref().regenerator(), run.ref().mesher()));
    WARN(std::format("ThermalSurfaceFlux in one sequence: remesh, conductivity, load, boundary condition, undo, solver settings -- six "
                     "edits, six stale verdicts, seven runs, the first result stale at the end (fingerprints 0x{:x} -> 0x{:x})",
                     original, fingerprintOf(*r6)));
}

TEST_CASE("P18Qualification_TheTransientEditsAndTheCapacityStaleTheirOwnObjects", "[p18qual][currentness][command][transient]") {
    // The same question for the TRANSIENT inputs, which the steady document above does not have: the initial temperature, the time
    // step, and the two material properties only a transient reads (density, specific heat).
    //
    // ThermalTransientSlab: a held cap, two different convections, a uniform initial field.
    auto built = reference::buildThermalTransientSlabReferenceModel();
    REQUIRE(built.has_value());
    Run run(std::move(built->document));
    CommandHistory history;
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r0 = run.takeRun();
    CHECK(staleSubjects(judge(run, *r0)).empty());
    REQUIRE(r0->transient.has_value());

    // 1. THE INITIAL TEMPERATURE: the history and what hangs off it, nothing upstream.
    execute<thermal::SetInitialTemperatureCommand>(run, history, run.analysis(),
                                                   thermal::UniformInitialTemperature{Temperature::fromSi(310.0)});
    CHECK(staleSubjects(judge(run, *r0)) == kFromTheSolution);
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run.takeRun();
    CHECK(fingerprintOf(*r1) != fingerprintOf(*r0));

    // 2. THE TIME STEP (and so the whole time grid).
    {
        thermal::TransientSettings settings = *definitionOf(run).transient;
        settings.timeStep = Time::fromSi(0.5 * settings.timeStep.si());
        execute<thermal::SetTransientSettingsCommand>(run, history, run.analysis(), settings);
    }
    CHECK(staleSubjects(judge(run, *r1)) == kFromTheSolution);
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r2 = run.takeRun();
    CHECK(r2->post->stateCount() == 21); // 10 steps became 20: the time list moved with the edit
    CHECK(judge(run, *r2).accepted());

    // 2b. A PRESCRIBED TEMPERATURE in a transient run: the boundary preparation and the history stale, the system does not (no
    //     Dirichlet elimination in base assembly). 2c. A CONVECTION coefficient: it enters K and Q, so the SYSTEM stales too.
    {
        const thermal::ThermalBoundaryCondition held = definitionOf(run).boundaryConditions[0];
        REQUIRE(held.prescribedTemperature() != nullptr);
        execute<thermal::EditThermalBoundaryConditionCommand>(
            run, history, run.analysis(),
            thermal::ThermalBoundaryCondition{held.id(), thermal::PrescribedTemperature{.face = held.face(), .value = Temperature::fromSi(390.0)}});
    }
    CHECK(staleSubjects(judge(run, *r2)) == plus(kFromTheSolution, {ValidationSubject::PreparedBoundary}));
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r2b = run.takeRun();
    CHECK(fingerprintOf(*r2b) != fingerprintOf(*r2));
    {
        const thermal::ThermalBoundaryCondition convecting = definitionOf(run).boundaryConditions[1];
        const thermal::ConvectionBoundary* convection = convecting.convection();
        REQUIRE(convection != nullptr);
        execute<thermal::EditThermalBoundaryConditionCommand>(
            run, history, run.analysis(),
            thermal::ThermalBoundaryCondition{convecting.id(), thermal::ConvectionBoundary{.face = convecting.face(),
                                                                                           .coefficient = HeatTransferCoefficient::fromSi(2.0 * convection->coefficient.si()),
                                                                                           .ambient = convection->ambient}});
    }
    CHECK(staleSubjects(judge(run, *r2b)) ==
          plus(kFromTheSolution, {ValidationSubject::PreparedBoundary, ValidationSubject::System}));
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r2c = run.takeRun();
    CHECK(fingerprintOf(*r2c) != fingerprintOf(*r2b));
    r2 = std::move(r2c); // the material edits below judge this one

    // 3. THE SPECIFIC HEAT: it enters the capacity matrix, so the SYSTEM is stale (K does not change, C does) and so is everything
    //    after it. A density edit goes through the mechanical half of the material and must do the same.
    const MaterialId material = assignedMaterial(run.document());
    {
        materials::ThermalProperties thermalProps = features::findMaterial(run.document(), material)->definition().thermal;
        thermalProps.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(900.0));
        REQUIRE(features::setMaterialThermal(run.document(), material, thermalProps).has_value());
    }
    CHECK(run.ref().currency() == meshing::MeshCurrency::Current);
    CHECK(staleSubjects(judge(run, *r2)) == plus(kFromTheSolution, {ValidationSubject::System}));
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r3 = run.takeRun();
    CHECK(fingerprintOf(*r3) != fingerprintOf(*r2));
    {
        materials::MechanicalProperties mechanical = features::findMaterial(run.document(), material)->definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(7000.0));
        REQUIRE(features::setMaterialMechanical(run.document(), material, mechanical).has_value());
    }
    CHECK(staleSubjects(judge(run, *r3)) == plus(kFromTheSolution, {ValidationSubject::System}));
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r4 = run.takeRun();
    CHECK(fingerprintOf(*r4) != fingerprintOf(*r3));

    // 4. UNDO twice (the time step, then the initial temperature): the intent is back to the state before step 2... and every
    //    result computed under a state the document has since left is still stale.
    REQUIRE(history.undo(run.document()).has_value());
    REQUIRE(history.undo(run.document()).has_value());
    CHECK(judge(run, *r0).accepted() == false);
    CHECK(judge(run, *r1).accepted() == false);
    CHECK(judge(run, *r4).accepted() == false);
    run.rerun();
    REQUIRE(run.run().accepted());
    CHECK(judge(run, *run.takeRun()).accepted());
}

TEST_CASE("P18Qualification_AFailedRegenerationNeverYieldsACurrentResult", "[p18qual][stale][currentness]") {
    // A regeneration that FAILS after a result was current, in sequence. The model is edited to something the CAD kernel cannot
    // build; the run is refused at the model stage with nothing published past it; the old result is stale; the model is restored,
    // regenerated and remeshed, and the new run agrees with the old one to the last bit -- while the OLD result stays stale,
    // because the mesh object moved.
    auto built = reference::buildThermalSurfaceFluxReferenceModel();
    REQUIRE(built.has_value());
    Run run(std::move(built->document));
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run.takeRun();
    const std::uint64_t original = fingerprintOf(*r1);

    const auto item = run.document().findByName("sf_l");
    REQUIRE(item.has_value());
    const auto parameter = run.document().asParameter(*item);
    REQUIRE(parameter.has_value());
    const auto zeroed = run.document().setParameterValue(*parameter, 0.0 * units::mm);
    // MEASURED: a zero depth is accepted as a parameter value and refused by the extrusion, so the regeneration FAILS. That is the
    // branch this case exists for, so it is a REQUIREMENT: were the parameter to refuse zero some day, this test must be rewritten to
    // find another unbuildable edit rather than silently stop exercising the failure.
    REQUIRE(zeroed.has_value());
    const features::RegenerationReport report = run.ref().regenerate();
    CHECK_FALSE(report.succeeded());
    run.rerun();
    const thermal::ThermalAnalysisRun& failed = run.run();
    INFO(describe(failed.validation));
    CHECK_FALSE(failed.accepted());
    CHECK(failed.stoppedAt.has_value());
    CHECK_FALSE(failed.steady.has_value());
    CHECK_FALSE(failed.post.has_value());
    CHECK_FALSE(failed.balance.has_value());
    CHECK_FALSE(judge(run, *r1).accepted());
    // RECOVERY at the original value
    REQUIRE(run.document().setParameterValue(*parameter, 100.0 * units::mm).has_value());
    REQUIRE(run.ref().regenerate().succeeded());
    run.remeshOnly();
    run.rerun();
    REQUIRE(run.run().accepted());
    CHECK(fingerprintOf(run.run()) == original);
    CHECK_FALSE(judge(run, *r1).accepted()); // the same numbers, a different mesh object: still stale
    CHECK(staleSubjects(judge(run, *r1)) == kEverything);
}


TEST_CASE("P18Qualification_AManualEditBackFollowsTheSameTwoRegimesAsUndo", "[p18qual][currentness][command]") {
    // THE NON-UNDO PATH of the two currentness regimes (ADR-055). Undo is not the only way back to an old state: the user can type the
    // old value again. The numerical objects are keyed by the CONTENT of their inputs, so they describe the document again; the
    // validation report is keyed by the analysis object's REVISION, which two edits have moved forward twice, so the old verdict never
    // revives. A fresh run of the restored intent reproduces the old arrays bit for bit, which is why content-keyed revival is sound.
    auto built = reference::buildThermalSurfaceFluxReferenceModel();
    REQUIRE(built.has_value());
    Run run(std::move(built->document));
    CommandHistory history;
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r0 = run.takeRun();
    const std::uint64_t original = fingerprintOf(*r0);

    const thermal::ThermalLoad load = definitionOf(run).loads.front();
    const thermal::SurfaceHeatFluxLoad* flux = load.surfaceHeatFlux();
    REQUIRE(flux != nullptr);
    const auto withFlux = [&](double watts) {
        return thermal::ThermalLoad{load.id(), thermal::SurfaceHeatFluxLoad{.face = flux->face, .flux = HeatFlux::fromSi(watts)}};
    };
    const double first = flux->flux.si();

    execute<thermal::EditThermalLoadCommand>(run, history, run.analysis(), withFlux(1.5 * first));
    CHECK(staleSubjects(judge(run, *r0)) == plus(kFromTheSolution, {ValidationSubject::System, ValidationSubject::PreparedLoads}));
    run.rerun();
    REQUIRE(run.run().accepted());
    std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run.takeRun();
    CHECK(fingerprintOf(*r1) != original);

    execute<thermal::EditThermalLoadCommand>(run, history, run.analysis(), withFlux(first)); // typed back, not undone
    CHECK(staleSubjects(judge(run, *r0)).empty());                        // the content-keyed objects describe the document again
    CHECK_FALSE(thermal::isCurrent(r0->validation, run.document(), run.ref().regenerator(), run.ref().mesher())); // the report does not
    CHECK_FALSE(staleSubjects(judge(run, *r1)).empty());                  // and the edited run is stale under the restored content
    run.rerun();
    REQUIRE(run.run().accepted());
    CHECK(fingerprintOf(run.run()) == original);                          // a fresh run reproduces the old arrays bit for bit
    CHECK(thermal::isCurrent(run.run().validation, run.document(), run.ref().regenerator(), run.ref().mesher()));
}

#endif // BETTERCAD_TESTS_EXPECT_NETGEN

TEST_CASE("P18Qualification_PossessionOfAThermalModelIsTheOnlyProof", "[p18qual][adr][arch]") {
    // ADR-051: `ThermalModel` has no public constructor and one friend, so holding one proves `requireThermalModel` accepted the
    // input. Nothing but the header said so; this makes it a build-time fact: it cannot be default-made, copied (a copy would be a
    // second proof of the same moment), or assembled from its parts by a caller.
    using namespace bettercad;
    using thermal::ThermalModel;
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<ThermalModel>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<ThermalModel>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<ThermalModel>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ThermalModel>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<ThermalModel, ObjectId, MeshControlId, const meshing::VolumeMesh&,
                                                 const meshing::GeometryMeshMap&, const meshing::MeshQualityReport&, ThermalConductivity,
                                                 meshing::GeometryRevision>);
    SUCCEED("the proof of an accepted input cannot be forged, copied or made from nothing");
}

TEST_CASE("P18Qualification_TheThermalTimeConstantIsATimeAndTheTransientSumIsDimensionallyLegal", "[p18qual][units]") {
    // The transient system matrix is K + C/dt. That sum is only legal if C/dt is a thermal conductance, and the lumped time constant
    // C/(hA) of the convection reference is a time. The units library would refuse the wrong ones at compile time; stating them
    // here is the evidence that the right ones are the ones in use.
    using namespace bettercad;
    using namespace bettercad::thermal;
    STATIC_REQUIRE(decltype(HeatCapacity{} / Time{})::dimension == ThermalConductance::dimension);
    STATIC_REQUIRE(decltype(HeatCapacity{} / (HeatTransferCoefficient{} * Area{}))::dimension == Time::dimension);
    STATIC_REQUIRE(decltype(Density{} * SpecificHeatCapacity{} * Volume{} / (HeatTransferCoefficient{} * Area{}))::dimension ==
                   Time::dimension);
    STATIC_REQUIRE(decltype(ThermalConductance{} * Temperature{})::dimension == Power::dimension);
    SUCCEED("C/dt is a conductance; C/(h A) is a time");
}

