// P18-CMD-001 -- what a successful, a refused, a no-op and an undone thermal command do to DERIVED state.
//
// THIS FILE ADDS NO INVALIDATION MECHANISM AND TESTS NONE. It asks the established currentness functions of each
// qualified milestone -- the prepared loads, the prepared boundary, the assembled system, the solution, the post
// result, the balance and the validation report -- whether each still describes the document, before and after a
// command, on a real mesh with a real solve. Nothing here consults anything a command invented, because a command
// invents nothing.
//
// TWO CURRENTNESS REGIMES EXIST IN THE QUALIFIED P18 CODE, and the undo cases are where they show:
//
//   BY VALUE      the prepared objects, the system, the solutions, the post result and the balance each record the
//                 canonical intent they were computed from (the loads, the conditions, the settings, the initial
//                 temperature, the transient settings, the mesh stamp, the geometry and material revisions) and are
//                 compared with the intent the document holds NOW. An undo that restores the intent exactly
//                 therefore makes an older result describe the document again -- correctly, since the result is a
//                 deterministic function of that very source.
//   BY REVISION   the validation report records the analysis OBJECT's revision (`ThermalResultSource`), and
//                 revisions are monotonic: an undo moves the revision forward, so a report never revives.
//
// The commands change neither regime. These tests record what the repository does, as it does it.

#include "ThermalCommandTestSupport.hpp"
#include "renderer/ThermalVizTestSupport.hpp"

#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <span>

using namespace bettercad::literals;
using namespace vztest;
using Catch::Matchers::ContainsSubstring;
using thermal::AddThermalBoundaryConditionCommand;
using thermal::AddThermalLoadCommand;
using thermal::EditThermalBoundaryConditionCommand;
using thermal::EditThermalLoadCommand;
using thermal::RemoveThermalBoundaryConditionCommand;
using thermal::RemoveThermalLoadCommand;
using thermal::SetInitialTemperatureCommand;
using thermal::SetThermalSolverSettingsCommand;
using thermal::SetTransientSettingsCommand;

namespace {

/// Which derived layers still describe the document. Every field is the answer of that milestone's own function.
struct Layers {
    bool mesh = false;
    bool loads = false;
    bool boundary = false;
    bool system = false;
    bool solution = false;
    bool post = false;
    bool balance = false;
    bool report = false;

    friend bool operator==(const Layers&, const Layers&) = default;
};

[[nodiscard]] Layers layersOf(const VizStudy& study, const Derived& derived, const thermal::ThermalValidationReport& report) {
    Layers out;
    out.mesh = meshing::describesTheModel(study.mesher.currency(study.document, study.control));
    out.report = thermal::isCurrent(report, study.document, study.regenerator, study.mesher);
    const Result<thermal::ThermalModel> model =
        thermal::requireThermalModel(study.document, study.regenerator, study.mesher, study.control);
    if (!model.has_value()) {
        return out;   // no mesh that describes the model: nothing else can be current against one
    }
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const std::span<const ThermalLoad> loads{def.loads};
    const std::span<const ThermalBoundaryCondition> conditions{def.boundaryConditions};
    out.loads = derived.loads.has_value() && thermal::isCurrent(*derived.loads, *model, loads);
    out.boundary = derived.boundary.has_value() && thermal::isCurrent(*derived.boundary, *model, conditions);
    out.system = derived.system.has_value() && thermal::isCurrent(*derived.system, study.document, *model, def.mode, loads, conditions);
    if (def.mode == ThermalAnalysisMode::Steady) {
        out.solution = derived.steady.has_value() &&
                       thermal::isCurrent(*derived.steady, study.document, *model, loads, conditions, def.solver);
        out.post = derived.post.has_value() &&
                   thermal::isCurrent(*derived.post, study.document, *model, loads, conditions, def.solver);
        out.balance = derived.balance.has_value() &&
                      thermal::isCurrent(*derived.balance, study.document, *model, loads, conditions, def.solver);
    } else {
        out.solution = derived.transient.has_value() && def.initialCondition.has_value() && def.transient.has_value() &&
                       thermal::isCurrent(*derived.transient, study.document, *model, loads, conditions,
                                          *def.initialCondition, *def.transient, def.solver);
        out.post = derived.post.has_value() && def.initialCondition.has_value() && def.transient.has_value() &&
                   thermal::isCurrent(*derived.post, study.document, *model, loads, conditions, *def.initialCondition,
                                      *def.transient, def.solver);
        out.balance = derived.balance.has_value() && def.initialCondition.has_value() && def.transient.has_value() &&
                      thermal::isCurrent(*derived.balance, study.document, *model, loads, conditions,
                                         *def.initialCondition, *def.transient, def.solver);
    }
    return out;
}

constexpr Layers kAllCurrent{true, true, true, true, true, true, true, true};

/// A solved STEADY study with every kind of canonical record: a face flux, a volumetric source, a held temperature and
/// a convecting face. Every derived layer is computed from it, and a validation report is taken.
struct SteadyBed {
    VizStudy study;
    CommandHistory history;
    std::unique_ptr<Derived> derived;
    std::optional<thermal::ThermalValidationReport> report;

    SteadyBed() {
        define(study, steadyDefinition(study,
                                       {surfaceLoad(1, study.side(0), 5000.0), generationLoad(2, 1.0e5)},
                                       {temperatureBc(1, study.startCap(), 400.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
        resolve();
    }

    void resolve() {
        derived = derive(study);
        REQUIRE(derived->stoppedBecause.empty());
        REQUIRE(derived->post.has_value());
        report = validationOf(study, *derived);
    }

    [[nodiscard]] Layers layers() const { return layersOf(study, *derived, *report); }

    template <typename C, typename... Args>
    [[nodiscard]] Result<void> run(Args&&... args) {
        return history.execute(study.document, std::make_unique<C>(std::forward<Args>(args)...));
    }
};

/// The transient counterpart: a held cap, a convecting cap and a source, from 300 K, 0.3 s steps to 1 s.
struct TransientBed {
    VizStudy study;
    CommandHistory history;
    std::unique_ptr<Derived> derived;
    std::optional<thermal::ThermalValidationReport> report;

    TransientBed() {
        define(study, transientDefinition(study, {generationLoad(2, 1.0e5)},
                                          {temperatureBc(1, study.startCap(), 400.0), convectionBc(2, study.endCap(), 25.0, kAmbient)},
                                          300.0, 0.3, 1.0));
        derived = derive(study);
        REQUIRE(derived->stoppedBecause.empty());
        REQUIRE(derived->post.has_value());
        report = validationOf(study, *derived);
    }

    [[nodiscard]] Layers layers() const { return layersOf(study, *derived, *report); }

    template <typename C, typename... Args>
    [[nodiscard]] Result<void> run(Args&&... args) {
        return history.execute(study.document, std::make_unique<C>(std::forward<Args>(args)...));
    }
};

//                       mesh   loads  bound  system soln   post   bal    report
constexpr Layers kLoadEdit{true, false, true, false, false, false, false, false};
constexpr Layers kHeldTemperatureEdit{true, true, false, true, false, false, false, false};
constexpr Layers kConvectionEdit{true, true, false, false, false, false, false, false};
constexpr Layers kSettingsEdit{true, true, true, true, false, false, false, false};

} // namespace

// ===========================================================================
// The invalidation matrix: which layers a successful command makes stale
// ===========================================================================

TEST_CASE("ThermalCommand_LoadMutationStalesResult", "[thermal][command][currentness][load]") {
    // The precondition first: every layer is current, so "no longer current" is a transition and not a standing state.
    SteadyBed bed;
    REQUIRE(bed.layers() == kAllCurrent);

    SECTION("add a load") {
        REQUIRE(bed.run<AddThermalLoadCommand>(bed.study.analysis, generationLoad(3, 2.0e4)).has_value());
    }
    SECTION("edit a load by one part in a million") {
        REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 5000.005)).has_value());
    }
    SECTION("edit a load's face") {
        REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(1), 5000.0)).has_value());
    }
    SECTION("remove a load") {
        REQUIRE(bed.run<RemoveThermalLoadCommand>(bed.study.analysis, ThermalLoadId::fromValue(2)).has_value());
    }
    // WHAT GOES STALE: the prepared loads, the assembled system (Q), the solution, the recovered fields, the balance and
    // the validation report. WHAT DOES NOT: the P16 mesh, and the prepared boundary (the loads are not in its source).
    CHECK(bed.layers() == kLoadEdit);
}

TEST_CASE("ThermalCommand_LoadEditDoesNotInvalidateMesh", "[thermal][command][currentness][load][mesh]") {
    SteadyBed bed;
    const meshing::MeshStamp stamp = bed.study.mesh().stamp();
    const meshing::MeshCurrency before = bed.study.mesher.currency(bed.study.document, bed.study.control);
    REQUIRE(meshing::describesTheModel(before));
    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 123.0)).has_value());
    CHECK(meshing::describesTheModel(bed.study.mesher.currency(bed.study.document, bed.study.control)));
    CHECK(bed.study.mesh().stamp() == stamp);   // the SAME generation: nothing remeshed
    CHECK(bed.study.mesher.mesh(bed.study.control) != nullptr);
}

TEST_CASE("ThermalCommand_BCMutationStalesResult", "[thermal][command][currentness][boundary]") {
    SteadyBed bed;
    REQUIRE(bed.layers() == kAllCurrent);
    Layers expected = kHeldTemperatureEdit;

    SECTION("add a prescribed temperature") {
        REQUIRE(bed.run<AddThermalBoundaryConditionCommand>(bed.study.analysis, temperatureBc(3, bed.study.side(2), 350.0)).has_value());
    }
    SECTION("edit a prescribed temperature's value (the constrained set is the same)") {
        REQUIRE(bed.run<EditThermalBoundaryConditionCommand>(bed.study.analysis, temperatureBc(1, bed.study.startCap(), 401.0)).has_value());
    }
    SECTION("edit a prescribed temperature's face (the constrained set moves)") {
        REQUIRE(bed.run<EditThermalBoundaryConditionCommand>(bed.study.analysis, temperatureBc(1, bed.study.side(1), 400.0)).has_value());
    }
    SECTION("remove a convection") {
        REQUIRE(bed.run<RemoveThermalBoundaryConditionCommand>(bed.study.analysis, ThermalBoundaryConditionId::fromValue(2)).has_value());
        expected = kConvectionEdit;
    }
    SECTION("edit a convection's coefficient") {
        REQUIRE(bed.run<EditThermalBoundaryConditionCommand>(bed.study.analysis, convectionBc(2, bed.study.endCap(), 26.0, kAmbient)).has_value());
        expected = kConvectionEdit;
    }
    SECTION("edit a convection's ambient") {
        REQUIRE(bed.run<EditThermalBoundaryConditionCommand>(bed.study.analysis, convectionBc(2, bed.study.endCap(), 25.0, kAmbient + 5.0)).has_value());
        expected = kConvectionEdit;
    }
    SECTION("add a convection") {
        REQUIRE(bed.run<AddThermalBoundaryConditionCommand>(bed.study.analysis, convectionBc(3, bed.study.side(2), 12.0, kAmbient)).has_value());
        expected = kConvectionEdit;
    }
    // A HELD TEMPERATURE is not in K or Q: it is a constraint, so the assembled system survives it and the solution does
    // not. A CONVECTION adds Robin terms to K and Q, so the system goes too. Neither touches the P16 mesh or the loads.
    CHECK(bed.layers() == expected);
}

TEST_CASE("ThermalCommand_BoundaryEditDoesNotInvalidateMesh", "[thermal][command][currentness][boundary][mesh]") {
    SteadyBed bed;
    const meshing::MeshStamp stamp = bed.study.mesh().stamp();
    REQUIRE(bed.run<EditThermalBoundaryConditionCommand>(bed.study.analysis, temperatureBc(1, bed.study.startCap(), 390.0)).has_value());
    CHECK(meshing::describesTheModel(bed.study.mesher.currency(bed.study.document, bed.study.control)));
    CHECK(bed.study.mesh().stamp() == stamp);
}

TEST_CASE("ThermalCommand_SolverSettingsMutationStalesResult", "[thermal][command][currentness][solver]") {
    SteadyBed bed;
    REQUIRE(bed.layers() == kAllCurrent);
    REQUIRE(bed.run<SetThermalSolverSettingsCommand>(bed.study.analysis, ThermalSolverSettings{.relativeResidualTolerance = 1.0e-10}).has_value());
    // A tolerance is not a meshing question and not a K or Q question: the mesh, the prepared objects and the assembled
    // system survive; the result, and everything computed from it, does not.
    CHECK(bed.layers() == kSettingsEdit);
}

TEST_CASE("ThermalCommand_InitialConditionMutationStalesTransientResult", "[thermal][command][currentness][initial]") {
    TransientBed bed;
    REQUIRE(bed.layers() == kAllCurrent);
    REQUIRE(bed.run<SetInitialTemperatureCommand>(bed.study.analysis, thermal::UniformInitialTemperature{Temperature::fromSi(310.0)}).has_value());
    // The initial temperature is in no K, C or Q: the transient result and what follows from it go, the rest stays.
    CHECK(bed.layers() == kSettingsEdit);
}

TEST_CASE("ThermalCommand_TransientSettingsMutationStalesTransientResult", "[thermal][command][currentness][transient]") {
    TransientBed bed;
    REQUIRE(bed.layers() == kAllCurrent);

    SECTION("the time step") {
        REQUIRE(bed.run<SetTransientSettingsCommand>(bed.study.analysis, cmdtest::transientOf(0.25, 1.0, thermal::TransientOutputPolicy::EveryStep)).has_value());
    }
    SECTION("the end time") {
        REQUIRE(bed.run<SetTransientSettingsCommand>(bed.study.analysis, cmdtest::transientOf(0.3, 1.5, thermal::TransientOutputPolicy::EveryStep)).has_value());
    }
    SECTION("the output policy alone") {
        REQUIRE(bed.run<SetTransientSettingsCommand>(bed.study.analysis, cmdtest::transientOf(0.3, 1.0, thermal::TransientOutputPolicy::FinalOnly)).has_value());
    }
    // The mesh, K, C and Q are untouched by a time discretisation; the transient solution, its recovered fields, its
    // balance and the report are not.
    CHECK(bed.layers() == kSettingsEdit);
}

TEST_CASE("ThermalCommand_ATransientLoadEditStalesTheTransientResult", "[thermal][command][currentness][transient][load]") {
    TransientBed bed;
    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, generationLoad(2, 2.0e5)).has_value());
    CHECK(bed.layers() == kLoadEdit);
}

TEST_CASE("ThermalCommand_NoCommandStalesAnUnrelatedLayer", "[thermal][command][currentness][granularity]") {
    // The matrix above, read as a claim about granularity: a command stales exactly the layers whose recorded source
    // contains what it changed, so the four kinds of edit must leave DIFFERENT layers current -- if two rows were equal the
    // matrix would be a single answer repeated. Each is measured above on a real solve; here they are required to differ.
    CHECK(kLoadEdit != kHeldTemperatureEdit);
    CHECK(kLoadEdit != kConvectionEdit);
    CHECK(kLoadEdit != kSettingsEdit);
    CHECK(kHeldTemperatureEdit != kConvectionEdit);
    CHECK(kHeldTemperatureEdit != kSettingsEdit);
    CHECK(kConvectionEdit != kSettingsEdit);
    // The loosest command stales the fewest layers and the widest the most, with the mesh surviving all of them.
    const auto stale = [](const Layers& l) {
        return static_cast<int>(!l.loads) + !l.boundary + !l.system + !l.solution + !l.post + !l.balance + !l.report;
    };
    CHECK(stale(kSettingsEdit) < stale(kHeldTemperatureEdit));
    CHECK(stale(kHeldTemperatureEdit) < stale(kConvectionEdit));
    for (const Layers& row : {kLoadEdit, kHeldTemperatureEdit, kConvectionEdit, kSettingsEdit}) {
        CHECK(row.mesh);
    }
}
// ===========================================================================
// What a refused command and a no-op command do NOT do
// ===========================================================================

TEST_CASE("ThermalCommand_FailedMutationPreservesCurrentResult", "[thermal][command][currentness][atomic]") {
    // One of every shape of refusal. Nothing changed, so nothing is stale -- and an invalid edit must not cost the user a
    // solve. An implementation that staled the result on ATTEMPT would pass every invalidation case above and fail here.
    SteadyBed bed;
    REQUIRE(bed.layers() == kAllCurrent);
    const std::uint64_t revision = thermal::findThermalAnalysis(bed.study.document, bed.study.analysis)->revision();
    const std::uint64_t documentRevision = bed.study.document.revision();
    const std::size_t depth = bed.history.undoCount();
    const std::uint64_t intent = cmdtest::fingerprintOf(bed.study.definition());

    CHECK_FALSE(bed.run<AddThermalLoadCommand>(bed.study.analysis, generationLoad(2, 9.0)).has_value());                      // duplicate id
    CHECK_FALSE(bed.run<AddThermalLoadCommand>(bed.study.analysis, generationLoad(7, cmdtest::kNaN)).has_value());            // NaN
    CHECK_FALSE(bed.run<EditThermalLoadCommand>(bed.study.analysis, generationLoad(77, 1.0)).has_value());                    // missing target
    CHECK_FALSE(bed.run<RemoveThermalBoundaryConditionCommand>(bed.study.analysis, ThermalBoundaryConditionId::fromValue(77)).has_value());
    CHECK_FALSE(bed.run<AddThermalBoundaryConditionCommand>(bed.study.analysis, convectionBc(9, bed.study.side(1), -1.0, kAmbient)).has_value());
    CHECK_FALSE(bed.run<SetThermalSolverSettingsCommand>(bed.study.analysis, ThermalSolverSettings{.relativeResidualTolerance = -1.0}).has_value());
    CHECK_FALSE(bed.run<SetInitialTemperatureCommand>(bed.study.analysis, thermal::UniformInitialTemperature{Temperature::fromSi(300.0)}).has_value());   // steady
    CHECK_FALSE(bed.run<SetTransientSettingsCommand>(bed.study.analysis, cmdtest::transientOf(0.1, 1.0)).has_value());                           // steady

    CHECK(bed.layers() == kAllCurrent);
    CHECK(thermal::findThermalAnalysis(bed.study.document, bed.study.analysis)->revision() == revision);
    CHECK(bed.study.document.revision() == documentRevision);
    CHECK(bed.history.undoCount() == depth);
    CHECK(cmdtest::fingerprintOf(bed.study.definition()) == intent);
    // ... and the viewer layer, which reads only the core, still shows a current, accepted result.
    const renderer::ThermalDisplayState state = displayStateOf(bed.study, bed.derived.get(), &*bed.report);
    CHECK(state.result == renderer::ThermalResultState::Current);
    CHECK(state.badge == renderer::ThermalValidationBadge::Pass);
    CHECK(renderer::thermalFieldAvailability(state) == renderer::ThermalFieldAvailability::Available);
}

TEST_CASE("ThermalCommand_ANoOpEditDoesNotStaleACurrentResult", "[thermal][command][currentness][noop]") {
    SteadyBed bed;
    const std::uint64_t revision = thermal::findThermalAnalysis(bed.study.document, bed.study.analysis)->revision();
    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 5000.0)).has_value());               // the same load
    REQUIRE(bed.run<SetThermalSolverSettingsCommand>(bed.study.analysis, ThermalSolverSettings{}).has_value());                       // the same settings
    REQUIRE(bed.run<EditThermalBoundaryConditionCommand>(bed.study.analysis, temperatureBc(1, bed.study.startCap(), 400.0)).has_value());
    CHECK(thermal::findThermalAnalysis(bed.study.document, bed.study.analysis)->revision() == revision);
    CHECK(bed.layers() == kAllCurrent);
    CHECK(displayStateOf(bed.study, bed.derived.get(), &*bed.report).result == renderer::ThermalResultState::Current);
}

TEST_CASE("ThermalCommand_ACommandRunsNoSolveAndNoRemesh", "[thermal][command][currentness][autosolve]") {
    // The held derived objects are values the test owns; a command cannot reach them. What can be observed is that the
    // mesh was not regenerated and that no object appeared in the document.
    SteadyBed bed;
    const meshing::MeshStamp stamp = bed.study.mesh().stamp();
    const std::size_t objects = bed.study.document.objectCount();
    const thermal::ThermalPostSource source = bed.derived->post->source();
    const thermal::ThermalPostResult copy = *bed.derived->post;
    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, generationLoad(2, 3.0e5)).has_value());
    REQUIRE(bed.run<AddThermalBoundaryConditionCommand>(bed.study.analysis, temperatureBc(3, bed.study.side(2), 350.0)).has_value());
    REQUIRE(bed.run<SetThermalSolverSettingsCommand>(bed.study.analysis, ThermalSolverSettings{.pivotFloor = 1.0e-11}).has_value());
    CHECK(bed.study.mesh().stamp() == stamp);
    CHECK(bed.study.document.objectCount() == objects);
    CHECK(bed.derived->post->source() == source);   // the old result is exactly what it was: stale, not replaced
    CHECK(*bed.derived->post == copy);
}

// ===========================================================================
// Undo and redo, against the two currentness regimes
// ===========================================================================

TEST_CASE("ThermalCommand_UndoCurrentnessFollowsSourceIdentity", "[thermal][command][currentness][undo]") {
    SteadyBed bed;
    REQUIRE(bed.layers() == kAllCurrent);
    const std::uint64_t r1 = thermal::findThermalAnalysis(bed.study.document, bed.study.analysis)->revision();
    const thermal::ThermalPostResult solvedBefore = *bed.derived->post;

    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 9000.0)).has_value());
    const std::uint64_t r2 = thermal::findThermalAnalysis(bed.study.document, bed.study.analysis)->revision();
    REQUIRE(bed.layers() == kLoadEdit);   // the old result is stale while the edit stands
    REQUIRE(r2 > r1);

    REQUIRE(bed.history.undo(bed.study.document).has_value());
    const std::uint64_t r3 = thermal::findThermalAnalysis(bed.study.document, bed.study.analysis)->revision();

    // THE CANONICAL CONTENT IS BACK EXACTLY. The revision is not: it moved forward again.
    CHECK(r3 > r2);
    CHECK(r3 != r1);

    // BY VALUE: every derived layer records the intent it was computed from, the document holds that intent again, and
    // the mesh, geometry and material are the same -- so the old result describes the document, and it is not a guess:
    // a fresh solve of this very source produces the same arrays bit for bit (asserted below).
    const Layers afterUndo = bed.layers();
    CHECK(afterUndo.mesh);
    CHECK(afterUndo.loads);
    CHECK(afterUndo.boundary);
    CHECK(afterUndo.system);
    CHECK(afterUndo.solution);
    CHECK(afterUndo.post);
    CHECK(afterUndo.balance);
    // BY REVISION: the validation report recorded the analysis object's revision, which an undo does not restore.
    CHECK_FALSE(afterUndo.report);

    // Not resurrection of a guess: the arrays an old result holds ARE the arrays a new solve produces.
    const std::unique_ptr<Derived> fresh = derive(bed.study);
    REQUIRE(fresh->post.has_value());
    CHECK(*fresh->post == solvedBefore);
    CHECK(fresh->post->source() == solvedBefore.source());
}

TEST_CASE("ThermalCommand_RedoCurrentnessFollowsSourceIdentity", "[thermal][command][currentness][redo]") {
    SteadyBed bed;
    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 9000.0)).has_value());
    REQUIRE(bed.history.undo(bed.study.document).has_value());
    REQUIRE(bed.layers().post);                       // current again by value
    REQUIRE(bed.history.redo(bed.study.document).has_value());
    // The edit stands again, so the result of the ORIGINAL source is stale again, by value.
    const Layers afterRedo = bed.layers();
    CHECK_FALSE(afterRedo.loads);
    CHECK_FALSE(afterRedo.system);
    CHECK_FALSE(afterRedo.solution);
    CHECK_FALSE(afterRedo.post);
    CHECK_FALSE(afterRedo.balance);
    CHECK_FALSE(afterRedo.report);
    CHECK(afterRedo.mesh);
    // And a result computed for the edited intent is current under it.
    const std::unique_ptr<Derived> edited = derive(bed.study);
    REQUIRE(edited->post.has_value());
    CHECK(layersOf(bed.study, *edited, validationOf(bed.study, *edited)) == kAllCurrent);
}

TEST_CASE("ThermalCommand_ARevisionKeyedRecordNeverRevives", "[thermal][command][currentness][undo][revision]") {
    // The report is the revision-keyed record. It goes stale on the edit, stays stale across the undo, and a NEW report
    // for the restored intent is current -- the same sequence P17-CMD-001 proved for its result.
    SteadyBed bed;
    REQUIRE(bed.report->source.has_value());
    const thermal::ThermalResultSource firstSource = *bed.report->source;
    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 9000.0)).has_value());
    CHECK_FALSE(bed.layers().report);
    REQUIRE(bed.history.undo(bed.study.document).has_value());
    CHECK_FALSE(bed.layers().report);   // NOT revived by the intent coming back

    const Result<thermal::ThermalResultSource> now = thermal::currentResultSource(bed.study.document, bed.study.regenerator,
                                                                                  bed.study.mesher, bed.study.analysis);
    REQUIRE(now.has_value());
    CHECK(now->analysisRevision > firstSource.analysisRevision);
    const std::vector<thermal::StaleReason> why = thermal::staleReasons(firstSource, *now);
    CHECK(std::ranges::find(why, thermal::StaleReason::Analysis) != why.end());

    bed.resolve();   // a re-solve and a re-validation for the restored intent
    CHECK(bed.layers() == kAllCurrent);
}

TEST_CASE("ThermalCommand_ViewerSeesCommandsThroughTheCore", "[thermal][command][currentness][viz]") {
    // P18-CMD does not touch the renderer. After a canonical edit the viewer layer, reading only the core, shows the
    // stale state and the stale badge; after a refused edit it shows neither.
    SteadyBed bed;
    REQUIRE(displayStateOf(bed.study, bed.derived.get(), &*bed.report).result == renderer::ThermalResultState::Current);

    CHECK_FALSE(bed.run<EditThermalLoadCommand>(bed.study.analysis, generationLoad(55, 1.0)).has_value());
    const renderer::ThermalDisplayState refused = displayStateOf(bed.study, bed.derived.get(), &*bed.report);
    CHECK(refused.result == renderer::ThermalResultState::Current);
    CHECK(refused.badge == renderer::ThermalValidationBadge::Pass);

    REQUIRE(bed.run<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 8000.0)).has_value());
    const renderer::ThermalDisplayState edited = displayStateOf(bed.study, bed.derived.get(), &*bed.report);
    CHECK(edited.result == renderer::ThermalResultState::Stale);
    CHECK(edited.badge == renderer::ThermalValidationBadge::Stale);
    CHECK(renderer::thermalFieldAvailability(edited) == renderer::ThermalFieldAvailability::StaleResult);
    CHECK(edited.meshDescribesModel());   // stale result, current mesh: the asymmetry
    const auto has = [&edited](thermal::TransientStaleReason reason) {
        return std::ranges::find(edited.staleReasons, reason) != edited.staleReasons.end();
    };
    CHECK(has(thermal::TransientStaleReason::Loads));
    CHECK_FALSE(has(thermal::TransientStaleReason::Mesh));
    CHECK_FALSE(has(thermal::TransientStaleReason::Material));

    REQUIRE(bed.history.undo(bed.study.document).has_value());
    const renderer::ThermalDisplayState undone = displayStateOf(bed.study, bed.derived.get(), &*bed.report);
    CHECK(undone.result == renderer::ThermalResultState::Current);             // by value, as above
    CHECK(undone.badge == renderer::ThermalValidationBadge::Stale);           // the report is revision-keyed
}

// ===========================================================================
// Canonical intent survives a remesh; history size does not depend on the mesh
// ===========================================================================

TEST_CASE("ThermalCommand_HistorySurvivesARemesh", "[thermal][command][reference][remesh]") {
    // A load names a FaceName. Resolve it on mesh M1, remesh to M2, and undo and redo the command: the payload holds
    // the FaceName and nothing about either mesh, so it is valid on both, and preparation re-resolves it against M2.
    SteadyBed bed;
    const std::uint64_t intent = cmdtest::fingerprintOf(bed.study.definition());
    const meshing::MeshStamp m1 = bed.study.mesh().stamp();

    auto edit = std::make_unique<EditThermalLoadCommand>(bed.study.analysis, surfaceLoad(1, bed.study.side(0), 7777.0));
    const EditThermalLoadCommand* raw = edit.get();
    REQUIRE(bed.history.execute(bed.study.document, std::move(edit)).has_value());
    const std::uint64_t edited = cmdtest::fingerprintOf(bed.study.definition());
    REQUIRE(edited != intent);
    const thermal::ThermalAnalysisDefinition payloadBefore = *raw->before();
    const thermal::ThermalAnalysisDefinition payloadAfter = *raw->after();

    bed.study.remesh();   // M2: a new generation of the same body
    REQUIRE(bed.study.mesh().stamp() != m1);
    CHECK(cmdtest::fingerprintOf(bed.study.definition()) == edited);   // the canonical intent did not move

    REQUIRE(bed.history.undo(bed.study.document).has_value());   // undo AFTER the remesh: no assumption about M1
    CHECK(cmdtest::fingerprintOf(bed.study.definition()) == intent);
    REQUIRE(bed.history.redo(bed.study.document).has_value());
    CHECK(cmdtest::fingerprintOf(bed.study.definition()) == edited);
    CHECK(*raw->before() == payloadBefore);   // the payload is the same two definitions, untouched by the remesh
    CHECK(*raw->after() == payloadAfter);

    // The face resolves on M2 by name: preparation works on the new mesh.
    const thermal::ThermalModel model = bed.study.model();
    Result<thermal::ThermalDofMap> map = thermal::buildThermalDofMap(model.mesh().mesh());
    REQUIRE(map.has_value());
    CHECK(thermal::prepareThermalLoads(model, *map, bed.study.definition()).has_value());
}

TEST_CASE("ThermalCommand_HistorySizeIndependentOfThermalResultSize", "[thermal][command][history][memory]") {
    // The SAME edit on a tiny mesh and on a finer one with a far larger result. The command's whole payload is two
    // canonical definitions, and they are equal across the two documents: nothing in them grows with the mesh.
    const auto edited = [](VizStudy& study) {
        define(study, steadyDefinition(study, {surfaceLoad(1, study.side(0), 5000.0)},
                                       {temperatureBc(1, study.startCap(), 400.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
        const std::unique_ptr<Derived> held = derive(study);
        REQUIRE(held->post.has_value());
        CommandHistory history;
        auto command = std::make_unique<EditThermalLoadCommand>(study.analysis, surfaceLoad(1, study.side(0), 6000.0));
        const EditThermalLoadCommand* raw = command.get();
        REQUIRE(history.execute(study.document, std::move(command)).has_value());
        struct Out {
            thermal::ThermalAnalysisDefinition before;
            thermal::ThermalAnalysisDefinition after;
            std::size_t nodes;
            std::size_t elements;
            std::size_t resultValues;
        };
        const thermal::ThermalPostState& state = held->post->states().front();
        return Out{*raw->before(), *raw->after(), study.mesh().nodes().size(), study.mesh().tetrahedra().size(),
                   state.temperatures.size() + (state.fields.gradients.size() + state.fields.heatFluxes.size()) * 3U};
    };
    VizStudy tiny;
    VizStudy fine(20_mm, 2_mm);
    const auto small = edited(tiny);
    const auto large = edited(fine);

    // The documents really differ in size...
    CHECK(large.nodes > small.nodes * 4U);
    CHECK(large.elements > small.elements * 10U);
    CHECK(large.resultValues > small.resultValues * 10U);
    // ... and the commands' payloads do not: the same two definitions, with the same number of records.
    CHECK(cmdtest::fingerprintOf(small.before) == cmdtest::fingerprintOf(large.before));
    CHECK(cmdtest::fingerprintOf(small.after) == cmdtest::fingerprintOf(large.after));
    CHECK(small.before.loads.size() == large.before.loads.size());
    CHECK(small.after.boundaryConditions.size() == large.after.boundaryConditions.size());
    WARN(std::format("history payload: {} loads + {} conditions in both; result values {} vs {}", small.after.loads.size(),
                     small.after.boundaryConditions.size(), small.resultValues, large.resultValues));
}

TEST_CASE("ThermalCommand_ACommandPermitsAConflictThatPreparationReports", "[thermal][command][boundary][policy]") {
    // The command layer invents no conflict policy: two different temperatures held on one face are canonical intent and
    // are accepted here. P18-BC owns what they mean, and says so when the boundary is prepared.
    VizStudy study;
    define(study, steadyDefinition(study, {}, {temperatureBc(1, study.startCap(), 400.0)}));
    CommandHistory history;
    REQUIRE(history.execute(study.document, std::make_unique<AddThermalBoundaryConditionCommand>(
                                                study.analysis, temperatureBc(2, study.startCap(), 450.0))).has_value());
    const thermal::ThermalModel model = study.model();
    Result<thermal::ThermalDofMap> map = thermal::buildThermalDofMap(model.mesh().mesh());
    REQUIRE(map.has_value());
    const auto prepared = thermal::prepareThermalBoundary(model, *map, study.definition());
    CHECK_FALSE(prepared.has_value());   // the conflict is P18-BC's to refuse
}

TEST_CASE("ThermalCommand_TransientUndoRedoFollowTheSameTwoRegimes", "[thermal][command][currentness][undo][redo][transient]") {
    // The undo/redo currentness above was asserted on a steady analysis. A transient one has more recorded intent (the
    // initial temperature and the time discretisation), and the two regimes must land the same way.
    TransientBed bed;
    REQUIRE(bed.layers() == kAllCurrent);
    const thermal::ThermalPostResult solvedBefore = *bed.derived->post;

    REQUIRE(bed.run<SetTransientSettingsCommand>(bed.study.analysis, cmdtest::transientOf(0.25, 1.0, thermal::TransientOutputPolicy::EveryStep)).has_value());
    REQUIRE(bed.layers() == kSettingsEdit);   // the old transient result is stale while the edit stands

    REQUIRE(bed.history.undo(bed.study.document).has_value());
    const Layers undone = bed.layers();
    CHECK(undone.solution);
    CHECK(undone.post);
    CHECK(undone.balance);
    CHECK_FALSE(undone.report);   // revision-keyed
    const std::unique_ptr<Derived> fresh = derive(bed.study);
    REQUIRE(fresh->post.has_value());
    CHECK(*fresh->post == solvedBefore);   // the arrays an old result holds are the arrays a new solve produces

    REQUIRE(bed.history.redo(bed.study.document).has_value());
    const Layers redone = bed.layers();
    CHECK_FALSE(redone.solution);
    CHECK_FALSE(redone.post);
    CHECK_FALSE(redone.balance);
    CHECK(redone.system);     // K, C and Q do not contain the time discretisation
    CHECK(redone.mesh);

    // The initial temperature goes the same way.
    REQUIRE(bed.history.undo(bed.study.document).has_value());
    REQUIRE(bed.run<SetInitialTemperatureCommand>(bed.study.analysis, thermal::UniformInitialTemperature{Temperature::fromSi(320.0)}).has_value());
    CHECK(bed.layers() == kSettingsEdit);
    REQUIRE(bed.history.undo(bed.study.document).has_value());
    CHECK(bed.layers().post);
}
