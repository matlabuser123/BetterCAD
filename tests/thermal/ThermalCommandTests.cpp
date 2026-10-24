// P18-CMD-001 -- undoable thermal-analysis edits (ADR-055).
//
// WHAT THESE TESTS HOLD, and it is separate properties that a single equality would blur:
//
//   1. CANONICAL CONTENT is restored exactly by undo and by redo -- ids, order, values, FaceNames, bit for bit.
//   2. A REFUSED COMMAND CHANGES NOTHING: no field, no revision, no history entry, no redo entry.
//   3. THE HISTORY HOLDS INTENT ONLY, shown by what a command can be constructed from and by what it stores.
//   4. THE DERIVED STATE responds through the established currentness system (ThermalCommandCurrentnessTests.cpp).
//
// A test that compared only (1) would pass while an atomicity bug shipped, and one that compared only (2) would
// pass while undo quietly renumbered a load. So there is an independent projection and fingerprint for (1) and a
// snapshot of every counter for (2), and no test uses one to stand in for the other.
//
// THE PROJECTION DELIBERATELY EXCLUDES THE REVISION. BetterCAD's revisions are monotonic: an edit moves them and an
// undo moves them again rather than rolling back. "Undo exact" is therefore a statement about CONTENT, and the
// revision is checked separately and in the opposite direction.

#include "ThermalCommandTestSupport.hpp"

#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalDof.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <functional>
#include <set>
#include <type_traits>

using namespace cmdtest;
using Catch::Matchers::ContainsSubstring;
using thermal::AddThermalBoundaryConditionCommand;
using thermal::AddThermalLoadCommand;
using thermal::CreateThermalAnalysisCommand;
using thermal::EditThermalBoundaryConditionCommand;
using thermal::EditThermalLoadCommand;
using thermal::RemoveThermalBoundaryConditionCommand;
using thermal::RemoveThermalLoadCommand;
using thermal::SetInitialTemperatureCommand;
using thermal::SetThermalSolverSettingsCommand;
using thermal::SetTransientSettingsCommand;
using thermal::ThermalAnalysisEditCommand;

// ===========================================================================
// Create
// ===========================================================================

TEST_CASE("ThermalCommand_CreateAnalysis", "[thermal][command][create]") {
    CmdPart part;
    const std::size_t before = part.document.objectCount();

    ThermalAnalysisDefinition wanted{.mesh = part.control};
    wanted.solver.relativeResidualTolerance = 3.5e-8;
    auto create = std::make_unique<CreateThermalAnalysisCommand>("Second", wanted);
    const CreateThermalAnalysisCommand* raw = create.get();
    REQUIRE_FALSE(raw->analysisId().isValid());   // invalid before a successful execute
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());

    const ThermalAnalysisId created = raw->analysisId();
    REQUIRE(created.isValid());
    CHECK(part.document.objectCount() == before + 1U);
    const ThermalAnalysis* found = thermal::findThermalAnalysis(part.document, created);
    REQUIRE(found != nullptr);
    // THE INTENT AS GIVEN, nothing defaulted on the way and nothing derived beside it.
    CHECK(CanonicalIntent{found->definition()} == CanonicalIntent{wanted});
    CHECK(found->definition().solver.relativeResidualTolerance == 3.5e-8);
    CHECK(part.history.undoCount() == 1U);
}

TEST_CASE("ThermalCommand_CreateAnalysisUndoExact", "[thermal][command][create][undo]") {
    CmdPart part;
    const std::size_t objects = part.document.objectCount();
    auto create = std::make_unique<CreateThermalAnalysisCommand>("Second", ThermalAnalysisDefinition{.mesh = part.control});
    const CreateThermalAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());
    const ThermalAnalysisId created = raw->analysisId();

    REQUIRE(part.history.undo(part.document).has_value());
    // BACK TO THE EXACT PREVIOUS DOCUMENT: no analysis, the same object count, and the other analysis untouched.
    CHECK(thermal::findThermalAnalysis(part.document, created) == nullptr);
    CHECK(part.document.objectCount() == objects);
    CHECK(thermal::thermalAnalysisIds(part.document) == std::vector<ThermalAnalysisId>{part.analysis});
    CHECK(part.history.redoCount() == 1U);
}

TEST_CASE("ThermalCommand_CreateAnalysisRedoExact", "[thermal][command][create][redo]") {
    CmdPart part;
    ThermalAnalysisDefinition wanted{.mesh = part.control};
    wanted.loads.push_back(surfaceLoad(4, part.endCap(), -250.0));
    wanted.boundaryConditions.push_back(heldTemperature(2, part.startCap(), 351.5));
    auto create = std::make_unique<CreateThermalAnalysisCommand>("Second", wanted);
    const CreateThermalAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());
    const ThermalAnalysisId created = raw->analysisId();
    const std::uint64_t madeAs = fingerprintOf(thermal::findThermalAnalysis(part.document, created)->definition());

    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.redo(part.document).has_value());
    const ThermalAnalysis* again = thermal::findThermalAnalysis(part.document, created);
    REQUIRE(again != nullptr);
    CHECK(fingerprintOf(again->definition()) == madeAs);
    CHECK(CanonicalIntent{again->definition()} == CanonicalIntent{wanted});
}

TEST_CASE("ThermalCommand_CreateAnalysisRedoPreservesId", "[thermal][command][create][redo][id]") {
    // THE ID IS ENGINEERING INTENT, NOT AN IMPLEMENTATION DETAIL. A result records which analysis it came from, so a
    // redo that minted a new ThermalAnalysisId would orphan every reference to the original -- and would do it on
    // every redo, so the id is checked across several round trips.
    CmdPart part;
    auto create = std::make_unique<CreateThermalAnalysisCommand>("Second", ThermalAnalysisDefinition{.mesh = part.control});
    const CreateThermalAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());
    const ThermalAnalysisId created = raw->analysisId();
    for (int round = 0; round < 5; ++round) {
        REQUIRE(part.history.undo(part.document).has_value());
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(raw->analysisId() == created);
        CHECK(thermal::findThermalAnalysis(part.document, created) != nullptr);
    }
    CHECK(thermal::thermalAnalysisIds(part.document).size() == 2U);
}

TEST_CASE("ThermalCommand_CreateAnalysisCreatesIntentOnly", "[thermal][command][create][derived]") {
    // The command takes a Document and nothing else: no Mesher, no Regenerator, no ThermalModel. So it cannot generate a
    // mesh, build a numbering or solve -- and what it adds to the document is exactly ONE object, the analysis, whose
    // definition is what it was given.
    CmdPart part;
    const std::size_t before = part.document.objectCount();
    std::set<ObjectId> idsBefore;
    for (const DocumentObject& object : part.document.objects()) {
        idsBefore.insert(object.id());
    }
    auto create = std::make_unique<CreateThermalAnalysisCommand>("Second", ThermalAnalysisDefinition{.mesh = part.control});
    const CreateThermalAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());
    CHECK(part.document.objectCount() == before + 1U);   // exactly one object more
    std::vector<ObjectId> added;
    for (const DocumentObject& object : part.document.objects()) {
        if (!idsBefore.contains(object.id())) {
            added.push_back(object.id());
        }
    }
    REQUIRE(added.size() == 1U);
    CHECK(added.front().value() == raw->analysisId().value());
    // The same type as the analysis the fixture already holds (the static name is not exported from a shared library).
    CHECK(part.document.findObject(added.front())->typeName() ==
          part.document.findObject(ObjectId::fromValue(part.analysis.value()))->typeName());
}
TEST_CASE("ThermalCommand_CreateAnalysisFailsAtomically", "[thermal][command][create][atomic]") {
    CmdPart part;
    const Snapshot before = snapshotOf(part);

    SECTION("unusable solver settings") {
        ThermalAnalysisDefinition bad{.mesh = part.control};
        bad.solver.relativeResidualTolerance = -1.0;
        CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("Bad", bad).has_value());
    }
    SECTION("the invalid mesh-control handle") {
        CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("Bad", ThermalAnalysisDefinition{}).has_value());
    }
    SECTION("a steady analysis that carries an initial temperature") {
        ThermalAnalysisDefinition bad{.mesh = part.control};
        bad.initialCondition = UniformInitialTemperature{Temperature::fromSi(300.0)};
        CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("Bad", bad).has_value());
    }
    SECTION("a duplicate load id") {
        ThermalAnalysisDefinition bad{.mesh = part.control};
        bad.loads = {generationLoad(1, 1.0), generationLoad(1, 2.0)};
        CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("Bad", bad).has_value());
    }
    SECTION("a name the document already uses") {
        CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("Study", ThermalAnalysisDefinition{.mesh = part.control}).has_value());
    }
    CHECK(snapshotOf(part) == before);
    CHECK_FALSE(part.history.canRedo());
}

// ===========================================================================
// Loads
// ===========================================================================

TEST_CASE("ThermalCommand_AddLoadUndoRedo", "[thermal][command][load]") {
    // THE BRIEF'S ORACLE, typed in: Analysis A, no loads; add a surface flux L7 = 1250 W/m^2 on a named face.
    CmdPart part;
    const FaceName wall = part.side();
    REQUIRE(part.definition().loads.empty());
    const std::uint64_t empty = part.fingerprint();

    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(7, wall, 1250.0)).has_value());
    REQUIRE(part.definition().loads.size() == 1U);
    const ThermalLoad& added = part.definition().loads.front();
    CHECK(added.id() == ThermalLoadId::fromValue(7));
    CHECK(added.kind() == thermal::ThermalLoadKind::SurfaceHeatFlux);
    CHECK(added.surfaceHeatFlux()->flux.si() == 1250.0);
    CHECK(added.target() == wall);
    const std::uint64_t full = part.fingerprint();
    CHECK(full != empty);

    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().loads.empty());
    CHECK(part.fingerprint() == empty);

    REQUIRE(part.history.redo(part.document).has_value());
    REQUIRE(part.definition().loads.size() == 1U);
    CHECK(part.definition().loads.front() == surfaceLoad(7, wall, 1250.0));   // the SAME id, face and bits
    CHECK(part.fingerprint() == full);
}

TEST_CASE("ThermalCommand_AddVolumetricLoadUndoRedo", "[thermal][command][load]") {
    CmdPart part;
    const std::uint64_t empty = part.fingerprint();
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(3, 4.0e5)).has_value());
    const std::uint64_t full = part.fingerprint();
    CHECK(part.definition().loads.front().volumetricHeatSource()->generation.si() == 4.0e5);
    CHECK_FALSE(part.definition().loads.front().target().has_value());   // a volumetric source has no face
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.fingerprint() == empty);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.fingerprint() == full);
}

TEST_CASE("ThermalCommand_EditLoadUndoRedo", "[thermal][command][load]") {
    // THE BRIEF'S ORACLE: L7 +1250 on face A becomes -300 on a DIFFERENT face B. Sign, target and id are all checked.
    CmdPart part;
    const FaceName faceA = part.side(0);
    const FaceName faceB = part.endCap();
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(7, faceA, 1250.0)).has_value());
    const std::uint64_t before = part.fingerprint();

    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, surfaceLoad(7, faceB, -300.0)).has_value());
    REQUIRE(part.definition().loads.size() == 1U);
    CHECK(part.definition().loads.front().id() == ThermalLoadId::fromValue(7));   // an edit keeps the id
    CHECK(part.definition().loads.front().surfaceHeatFlux()->flux.si() == -300.0);
    CHECK(part.definition().loads.front().target() == faceB);
    const std::uint64_t after = part.fingerprint();
    CHECK(after != before);

    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().loads.front() == surfaceLoad(7, faceA, 1250.0));   // sign, face and id, exactly
    CHECK(part.fingerprint() == before);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().loads.front() == surfaceLoad(7, faceB, -300.0));
    CHECK(part.fingerprint() == after);
}

TEST_CASE("ThermalCommand_EditLoadMayChangeItsKind", "[thermal][command][load]") {
    // The WHOLE record is replaced, so the variant changes with it: a surface flux becomes a volumetric source under
    // the same id, and undo gives the surface flux back with its FaceName.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(7, part.endCap(), 900.0)).has_value());
    const std::uint64_t before = part.fingerprint();
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(7, -2.5e4)).has_value());
    CHECK(part.definition().loads.front().kind() == thermal::ThermalLoadKind::VolumetricHeatSource);
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().loads.front().kind() == thermal::ThermalLoadKind::SurfaceHeatFlux);
    CHECK(part.definition().loads.front().target() == part.endCap());
    CHECK(part.fingerprint() == before);
}

TEST_CASE("ThermalCommand_RemoveLoadUndoRedo", "[thermal][command][load]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), 100.0)).has_value());
    const std::uint64_t one = part.fingerprint();
    REQUIRE(part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(1)).has_value());
    CHECK(part.definition().loads.empty());
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.fingerprint() == one);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().loads.empty());
}

TEST_CASE("ThermalCommand_MiddleRemovalRestoresExactCanonicalOrder", "[thermal][command][load][order]") {
    // L1 L2 L3 -> remove L2 -> L1 L3 -> undo -> L1 L2 L3 IN THAT ORDER. The order is the user's and is canonical state,
    // so a restored load appended at the end would be a different state, and the fingerprint hashes the vector in order.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.startCap(), 10.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 20.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(3, part.endCap(), 30.0)).has_value());
    const std::uint64_t all = part.fingerprint();

    REQUIRE(part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(2)).has_value());
    REQUIRE(part.definition().loads.size() == 2U);
    CHECK(part.definition().loads[0].id() == ThermalLoadId::fromValue(1));
    CHECK(part.definition().loads[1].id() == ThermalLoadId::fromValue(3));

    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.definition().loads.size() == 3U);
    CHECK(part.definition().loads[0].id() == ThermalLoadId::fromValue(1));
    CHECK(part.definition().loads[1].id() == ThermalLoadId::fromValue(2));
    CHECK(part.definition().loads[2].id() == ThermalLoadId::fromValue(3));
    CHECK(part.fingerprint() == all);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().loads.size() == 2U);
}

TEST_CASE("ThermalCommand_AnEditKeepsThePositionOfTheLoad", "[thermal][command][load][order]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 2.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(3, 3.0)).has_value());
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(2, 22.0)).has_value());
    CHECK(part.definition().loads[1].id() == ThermalLoadId::fromValue(2));   // still in the middle
    CHECK(part.definition().loads[1].volumetricHeatSource()->generation.si() == 22.0);
}

TEST_CASE("ThermalCommand_LoadIdPreserved", "[thermal][command][load][id]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(41, part.side(), 5.0)).has_value());
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, surfaceLoad(41, part.side(), 6.0)).has_value());
    for (int round = 0; round < 3; ++round) {
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(part.definition().loads.front().id() == ThermalLoadId::fromValue(41));
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(part.definition().loads.empty());
        REQUIRE(part.history.redo(part.document).has_value());
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(part.definition().loads.front().id() == ThermalLoadId::fromValue(41));
    }
}

TEST_CASE("ThermalCommand_LoadGeometryReferencePreserved", "[thermal][command][load][reference]") {
    // EVERY selector form the data model validates, and an unresolved one, round-trips as the FaceName it was given --
    // never as the facets it resolves to, never normalised, never repaired.
    CmdPart part;
    std::uint64_t id = 1;
    for (const FaceName& face : everyFaceForm(part.feature)) {
        INFO("face role " << toString(face.face.role) << " feature " << face.feature.value());
        const ThermalLoad load = surfaceLoad(id++, face, 77.0);
        const std::uint64_t before = part.fingerprint();
        REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, load).has_value());
        const std::uint64_t after = part.fingerprint();
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(part.fingerprint() == before);
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(part.fingerprint() == after);
        CHECK(part.definition().loads.back().target() == face);   // field for field, including copies
        CHECK(part.definition().loads.back() == load);
    }
}

TEST_CASE("ThermalCommand_LoadSignedMagnitudePreserved", "[thermal][command][load][sign]") {
    // A NEGATIVE flux is heat leaving and a NEGATIVE generation a sink: both are ordinary, neither is refused, and
    // neither is abs()'d or clamped by an edit or its undo.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), -1250.5)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, -8.0e3)).has_value());
    CHECK(part.definition().loads[0].surfaceHeatFlux()->flux.si() == -1250.5);
    CHECK(part.definition().loads[1].volumetricHeatSource()->generation.si() == -8.0e3);
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().loads[1].volumetricHeatSource()->generation.si() == -8.0e3);
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), 0.0)).has_value());   // zero is a load too
    CHECK(part.definition().loads[0].surfaceHeatFlux()->flux.si() == 0.0);
}

TEST_CASE("ThermalCommand_NoValueIsRoundTrippedThroughText", "[thermal][command][exact]") {
    // Values that a decimal round trip would change. An undo that formatted and re-parsed would lose these bits.
    CmdPart part;
    const double awkward = 0.1 + 0.2;   // 0.30000000000000004
    const double tiny = 4.9406564584124654e-324;   // the smallest subnormal
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, awkward)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, tiny)).has_value());
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(1, awkward * 1.0000000000000002)).has_value());
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(std::bit_cast<std::uint64_t>(part.definition().loads[0].volumetricHeatSource()->generation.si()) ==
          std::bit_cast<std::uint64_t>(awkward));
    CHECK(std::bit_cast<std::uint64_t>(part.definition().loads[1].volumetricHeatSource()->generation.si()) ==
          std::bit_cast<std::uint64_t>(tiny));
}

TEST_CASE("ThermalCommand_RepeatedUndoRedoDoesNotDrift", "[thermal][command][exact]") {
    // Undo and redo ASSIGN a stored definition; nothing is inverted. Fifty cycles must leave the value equal bit for
    // bit, which a `-= delta` / `+= delta` implementation could not.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), 1234.56789)).has_value());
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), -987.654321)).has_value());
    for (int cycle = 0; cycle < 50; ++cycle) {
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(part.definition().loads.front().surfaceHeatFlux()->flux.si() == 1234.56789);
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(part.definition().loads.front().surfaceHeatFlux()->flux.si() == -987.654321);
    }
}

TEST_CASE("ThermalCommand_EditMissingStableIdFails", "[thermal][command][load][atomic]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    const Snapshot before = snapshotOf(part);
    const Result<void> refused = part.run<EditThermalLoadCommand>(part.analysis, generationLoad(2, 5.0));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::NotFound);
    CHECK(snapshotOf(part) == before);
}

TEST_CASE("ThermalCommand_AnEditOfARemovedLoadDoesNotAliasTheNextOne", "[thermal][command][load][id]") {
    // L1 sat at position 0. After it is removed, L2 sits at position 0. An edit targeting L1 must fail -- not edit
    // whatever now occupies L1's old slot. The target is the stable id and never a vector index.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 2.0)).has_value());
    REQUIRE(part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(1)).has_value());
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(1, 99.0)).has_value());
    CHECK_FALSE(part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(1)).has_value());
    CHECK(snapshotOf(part) == before);
    CHECK(part.definition().loads.front().volumetricHeatSource()->generation.si() == 2.0);   // L2 untouched
}

TEST_CASE("ThermalCommand_FailedLoadCommandsAreAtomic", "[thermal][command][load][atomic]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    const Snapshot before = snapshotOf(part);

    SECTION("a duplicate id") {
        const Result<void> refused = part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 2.0));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::AlreadyExists);
    }
    SECTION("a NaN flux") {
        CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(2, part.side(), kNaN)).has_value());
    }
    SECTION("an infinite generation") {
        CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, kInf)).has_value());
        CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, -kInf)).has_value());
    }
    SECTION("an invalid load id") {
        CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, ThermalLoad{ThermalLoadId{}, VolumetricHeatSourceLoad{}}).has_value());
    }
    SECTION("a face selector the data model refuses (a side face with no entity)") {
        const FaceName malformed{part.feature, FaceSelector{.role = FaceRole::Side}};
        CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(2, malformed, 5.0)).has_value());
    }
    SECTION("an edit that would store a NaN") {
        CHECK_FALSE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(1, kNaN)).has_value());
    }
    SECTION("an analysis that does not exist") {
        CHECK_FALSE(part.run<AddThermalLoadCommand>(ThermalAnalysisId::fromValue(987654), generationLoad(2, 1.0)).has_value());
    }
    SECTION("a removal of a load that is not there") {
        CHECK_FALSE(part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(77)).has_value());
    }
    CHECK(snapshotOf(part) == before);
    CHECK(part.definition().loads.size() == 1U);
}

TEST_CASE("ThermalCommand_ALoadMayBeDefinedBeforeAnyMeshExists", "[thermal][command][load][reference]") {
    // The face is NOT resolved by the command: a user legitimately defines a load before a mesh exists, and a face of a
    // feature that does not exist is valid canonical intent (preparation reports it as unresolved).
    CmdPart part;
    const FaceName ghost{ObjectId::fromValue(987654), FaceSelector{.role = FaceRole::EndCap}};
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, ghost, 10.0)).has_value());
    CHECK(part.definition().loads.front().target() == ghost);
}

// ===========================================================================
// Boundary conditions
// ===========================================================================

TEST_CASE("ThermalCommand_AddPrescribedTemperatureUndoRedo", "[thermal][command][boundary][temperature]") {
    CmdPart part;
    const std::uint64_t empty = part.fingerprint();
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.startCap(), 350.0)).has_value());
    const std::uint64_t held = part.fingerprint();
    const ThermalBoundaryCondition& condition = part.definition().boundaryConditions.front();
    CHECK(condition.id() == ThermalBoundaryConditionId::fromValue(2));
    CHECK(condition.prescribedTemperature()->value.si() == 350.0);   // kelvin, as given: not 76.85, not 623.15
    CHECK(condition.face() == part.startCap());
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.fingerprint() == empty);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.fingerprint() == held);
    CHECK(part.definition().boundaryConditions.front() == heldTemperature(2, part.startCap(), 350.0));
}

TEST_CASE("ThermalCommand_EditPrescribedTemperatureUndoRedo", "[thermal][command][boundary][temperature]") {
    // The brief's oracle: 350 K on the start cap becomes 325 K on a DIFFERENT face.
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.startCap(), 350.0)).has_value());
    const std::uint64_t before = part.fingerprint();
    REQUIRE(part.run<EditThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.side(1), 325.0)).has_value());
    const std::uint64_t after = part.fingerprint();
    CHECK(part.definition().boundaryConditions.front().prescribedTemperature()->value.si() == 325.0);
    CHECK(part.definition().boundaryConditions.front().face() == part.side(1));
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().boundaryConditions.front() == heldTemperature(2, part.startCap(), 350.0));
    CHECK(part.fingerprint() == before);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.fingerprint() == after);
}

TEST_CASE("ThermalCommand_RemovePrescribedTemperatureUndoRedo", "[thermal][command][boundary][temperature]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.startCap(), 350.0)).has_value());
    const std::uint64_t held = part.fingerprint();
    REQUIRE(part.run<RemoveThermalBoundaryConditionCommand>(part.analysis, ThermalBoundaryConditionId::fromValue(2)).has_value());
    CHECK(part.definition().boundaryConditions.empty());
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.fingerprint() == held);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().boundaryConditions.empty());
}

TEST_CASE("ThermalCommand_AddConvectionUndoRedo", "[thermal][command][boundary][convection]") {
    // h = 25 W/(m^2 K), T_inf = 300 K on the outer face.
    CmdPart part;
    const std::uint64_t empty = part.fingerprint();
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(3, part.side(), 25.0, 300.0)).has_value());
    const std::uint64_t full = part.fingerprint();
    const thermal::ConvectionBoundary* held = part.definition().boundaryConditions.front().convection();
    REQUIRE(held != nullptr);
    CHECK(held->coefficient.si() == 25.0);
    CHECK(held->ambient.si() == 300.0);
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.fingerprint() == empty);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.fingerprint() == full);
}

TEST_CASE("ThermalCommand_EditConvectionUndoRedo", "[thermal][command][boundary][convection]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(3, part.side(), 25.0, 300.0)).has_value());
    const std::uint64_t before = part.fingerprint();
    REQUIRE(part.run<EditThermalBoundaryConditionCommand>(part.analysis, convecting(3, part.side(), 40.0, 290.0)).has_value());
    const std::uint64_t after = part.fingerprint();
    CHECK(part.definition().boundaryConditions.front().convection()->coefficient.si() == 40.0);
    CHECK(part.definition().boundaryConditions.front().convection()->ambient.si() == 290.0);
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().boundaryConditions.front() == convecting(3, part.side(), 25.0, 300.0));
    CHECK(part.fingerprint() == before);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.fingerprint() == after);
}

TEST_CASE("ThermalCommand_RemoveConvectionUndoRedo", "[thermal][command][boundary][convection]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(3, part.side(), 25.0, 300.0)).has_value());
    const std::uint64_t full = part.fingerprint();
    REQUIRE(part.run<RemoveThermalBoundaryConditionCommand>(part.analysis, ThermalBoundaryConditionId::fromValue(3)).has_value());
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.fingerprint() == full);
}

TEST_CASE("ThermalCommand_AnEditMayChangeTheKindOfCondition", "[thermal][command][boundary]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.endCap(), 310.0)).has_value());
    const std::uint64_t before = part.fingerprint();
    REQUIRE(part.run<EditThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.endCap(), 12.5, 295.0)).has_value());
    CHECK(part.definition().boundaryConditions.front().kind() == thermal::ThermalBoundaryConditionKind::Convection);
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().boundaryConditions.front().kind() == thermal::ThermalBoundaryConditionKind::PrescribedTemperature);
    CHECK(part.fingerprint() == before);
}

TEST_CASE("ThermalCommand_MiddleConditionRemovalRestoresExactCanonicalOrder", "[thermal][command][boundary][order]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 400.0)).has_value());
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), 20.0, 300.0)).has_value());
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(3, part.endCap(), 300.0)).has_value());
    const std::uint64_t all = part.fingerprint();
    REQUIRE(part.run<RemoveThermalBoundaryConditionCommand>(part.analysis, ThermalBoundaryConditionId::fromValue(2)).has_value());
    CHECK(part.definition().boundaryConditions.size() == 2U);
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.definition().boundaryConditions.size() == 3U);
    CHECK(part.definition().boundaryConditions[0].id() == ThermalBoundaryConditionId::fromValue(1));
    CHECK(part.definition().boundaryConditions[1].id() == ThermalBoundaryConditionId::fromValue(2));
    CHECK(part.definition().boundaryConditions[2].id() == ThermalBoundaryConditionId::fromValue(3));
    CHECK(part.fingerprint() == all);
}

TEST_CASE("ThermalCommand_BoundaryConditionIdPreserved", "[thermal][command][boundary][id]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(51, part.startCap(), 350.0)).has_value());
    REQUIRE(part.run<EditThermalBoundaryConditionCommand>(part.analysis, heldTemperature(51, part.startCap(), 360.0)).has_value());
    for (int round = 0; round < 3; ++round) {
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(part.definition().boundaryConditions.front().id() == ThermalBoundaryConditionId::fromValue(51));
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(part.definition().boundaryConditions.front().id() == ThermalBoundaryConditionId::fromValue(51));
    }
}

TEST_CASE("ThermalCommand_BoundaryGeometryReferencePreserved", "[thermal][command][boundary][reference]") {
    CmdPart part;
    std::uint64_t id = 1;
    for (const FaceName& face : everyFaceForm(part.feature)) {
        INFO("face role " << toString(face.face.role) << " feature " << face.feature.value());
        const ThermalBoundaryCondition held = heldTemperature(id++, face, 333.25);
        const ThermalBoundaryCondition convection = convecting(id++, face, 17.5, 288.75);
        for (const ThermalBoundaryCondition& condition : {held, convection}) {
            const std::uint64_t before = part.fingerprint();
            REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, condition).has_value());
            const std::uint64_t after = part.fingerprint();
            REQUIRE(part.history.undo(part.document).has_value());
            CHECK(part.fingerprint() == before);
            REQUIRE(part.history.redo(part.document).has_value());
            CHECK(part.fingerprint() == after);
            CHECK(part.definition().boundaryConditions.back().face() == face);
            CHECK(part.definition().boundaryConditions.back() == condition);
        }
    }
}

TEST_CASE("ThermalCommand_FailedBoundaryCommandsAreAtomic", "[thermal][command][boundary][atomic]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 300.0)).has_value());
    const Snapshot before = snapshotOf(part);

    SECTION("a duplicate id") {
        const Result<void> refused = part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(1, part.side(), 10.0, 300.0));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::AlreadyExists);
    }
    SECTION("a NaN prescribed temperature") {
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.endCap(), kNaN)).has_value());
    }
    SECTION("a temperature below absolute zero (no Celsius is accepted or converted)") {
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.endCap(), -40.0)).has_value());
    }
    SECTION("a zero convection coefficient") {
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), 0.0, 300.0)).has_value());
    }
    SECTION("a negative convection coefficient (not abs()'d)") {
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), -25.0, 300.0)).has_value());
    }
    SECTION("a NaN convection coefficient or ambient") {
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), kNaN, 300.0)).has_value());
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), 25.0, kNaN)).has_value());
    }
    SECTION("a multi-field edit with one bad field changes none") {
        // h valid, ambient invalid: the h must not land.
        REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), 25.0, 300.0)).has_value());
        const Snapshot withConvection = snapshotOf(part);
        CHECK_FALSE(part.run<EditThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), 99.0, -5.0)).has_value());
        CHECK(snapshotOf(part) == withConvection);
        CHECK(part.definition().boundaryConditions[1].convection()->coefficient.si() == 25.0);
        return;
    }
    SECTION("an edit of a condition that is not there") {
        CHECK_FALSE(part.run<EditThermalBoundaryConditionCommand>(part.analysis, heldTemperature(9, part.endCap(), 300.0)).has_value());
    }
    SECTION("a removal of a condition that is not there") {
        CHECK_FALSE(part.run<RemoveThermalBoundaryConditionCommand>(part.analysis, ThermalBoundaryConditionId::fromValue(9)).has_value());
    }
    SECTION("a face selector the data model refuses (a side face with no entity)") {
        const FaceName malformed{part.feature, FaceSelector{.role = FaceRole::Side}};
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, malformed, 300.0)).has_value());
    }
    CHECK(snapshotOf(part) == before);
}

TEST_CASE("ThermalCommand_TheCommandLayerInventsNoConflictPolicy", "[thermal][command][boundary][policy]") {
    // P18-BC and P18-VALID own what an identical duplicate and a conflicting pair mean. This layer permits the
    // canonical intent (under distinct ids) and never silently overwrites an existing condition.
    CmdPart part;
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 300.0)).has_value());
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, part.startCap(), 300.0)).has_value());   // identical
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(3, part.startCap(), 450.0)).has_value());   // conflicting
    REQUIRE(part.definition().boundaryConditions.size() == 3U);
    CHECK(part.definition().boundaryConditions[0].prescribedTemperature()->value.si() == 300.0);   // nothing overwritten
    CHECK(part.definition().boundaryConditions[2].prescribedTemperature()->value.si() == 450.0);
}

// ===========================================================================
// Initial temperature, solver settings, transient settings
// ===========================================================================

TEST_CASE("ThermalCommand_InitialTemperatureUndoRedoExact", "[thermal][command][initial]") {
    // Non-round decimals, to catch any loss through formatting. 300.125 K -> 347.875 K.
    CmdPart part{ThermalAnalysisMode::Transient};
    REQUIRE(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(300.125)}).has_value());
    const std::uint64_t first = part.fingerprint();
    REQUIRE(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(347.875)}).has_value());
    const std::uint64_t second = part.fingerprint();
    CHECK(part.definition().initialCondition->value.si() == 347.875);
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().initialCondition->value.si() == 300.125);   // bit for bit, not "close"
    CHECK(part.fingerprint() == first);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().initialCondition->value.si() == 347.875);
    CHECK(part.fingerprint() == second);
}

TEST_CASE("ThermalCommand_InitialTemperatureHoldsOneValueAndNoField", "[thermal][command][initial][derived]") {
    // THE CANONICAL INTENT IS ONE KELVIN VALUE. The derived initial vector is rebuilt by P18-TRANSIENT from it and the
    // numbering; the command's payload (before, after) holds the definition and the definition holds no vector.
    CmdPart part{ThermalAnalysisMode::Transient};
    auto command = std::make_unique<SetInitialTemperatureCommand>(
        part.analysis, UniformInitialTemperature{Temperature::fromSi(325.0)});
    const SetInitialTemperatureCommand* raw = command.get();
    REQUIRE(part.history.execute(part.document, std::move(command)).has_value());
    REQUIRE(raw->before().has_value());
    REQUIRE(raw->after().has_value());
    CHECK(raw->before()->initialCondition->value.si() == 300.0);
    CHECK(raw->after()->initialCondition->value.si() == 325.0);
    // Nothing else moved: the commands differ in the one field.
    ThermalAnalysisDefinition expected = *raw->before();
    expected.initialCondition = UniformInitialTemperature{Temperature::fromSi(325.0)};
    CHECK(*raw->after() == expected);
    static_assert(sizeof(UniformInitialTemperature) == sizeof(double));
}

TEST_CASE("ThermalCommand_InitialTemperatureIsRefusedForASteadyAnalysis", "[thermal][command][initial][mode]") {
    CmdPart part;   // steady
    const Snapshot before = snapshotOf(part);
    const Result<void> refused =
        part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(300.0)});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("steady"));
    CHECK(snapshotOf(part) == before);
    CHECK_FALSE(part.definition().initialCondition.has_value());
}

TEST_CASE("ThermalCommand_InvalidInitialTemperatureAtomic", "[thermal][command][initial][atomic]") {
    CmdPart part{ThermalAnalysisMode::Transient};
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(kNaN)}).has_value());
    CHECK_FALSE(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(kInf)}).has_value());
    CHECK_FALSE(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(-1.0)}).has_value());
    CHECK(snapshotOf(part) == before);
    CHECK(part.definition().initialCondition->value.si() == 300.0);
    // 0 K is the limit of the scale and is accepted (the data model's policy, reused rather than restated).
    CHECK(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(0.0)}).has_value());
}

TEST_CASE("ThermalCommand_SolverSettingsUndoRedoExact", "[thermal][command][solver]") {
    CmdPart part;
    const ThermalSolverSettings defaults = part.definition().solver;
    const ThermalSolverSettings distinctive{.relativeResidualTolerance = 3.7e-11, .pivotFloor = 2.5e-13};
    REQUIRE(part.run<SetThermalSolverSettingsCommand>(part.analysis, distinctive).has_value());
    CHECK(part.definition().solver == distinctive);
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().solver == defaults);   // the previous values, not a library default
    CHECK(part.definition().solver.relativeResidualTolerance == 1.0e-9);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().solver.relativeResidualTolerance == 3.7e-11);
    CHECK(part.definition().solver.pivotFloor == 2.5e-13);
}

TEST_CASE("ThermalCommand_InvalidSolverSettingsAtomic", "[thermal][command][solver][atomic]") {
    CmdPart part;
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{.relativeResidualTolerance = kNaN}).has_value());
    CHECK_FALSE(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{.relativeResidualTolerance = 0.0}).has_value());
    CHECK_FALSE(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{.relativeResidualTolerance = -1.0e-9}).has_value());
    CHECK_FALSE(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{.relativeResidualTolerance = kInf}).has_value());
    // A VALID residual with an INVALID pivot floor: neither may land.
    CHECK_FALSE(part.run<SetThermalSolverSettingsCommand>(
                    part.analysis, ThermalSolverSettings{.relativeResidualTolerance = 1.0e-6, .pivotFloor = kNaN}).has_value());
    CHECK(snapshotOf(part) == before);
    CHECK(part.definition().solver.relativeResidualTolerance == 1.0e-9);
}

TEST_CASE("ThermalCommand_TransientSettingsUndoRedoExact", "[thermal][command][transient]") {
    // The brief's oracle: dt = 0.3, t_end = 1.0 -> dt = 0.125, t_end = 2.0, and the output policy with it.
    CmdPart part{ThermalAnalysisMode::Transient};
    REQUIRE(part.definition().transient == transientOf(0.3, 1.0));
    const std::uint64_t before = part.fingerprint();
    REQUIRE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.125, 2.0, TransientOutputPolicy::EveryStep)).has_value());
    const std::uint64_t after = part.fingerprint();
    CHECK(part.definition().transient->timeStep.si() == 0.125);
    CHECK(part.definition().transient->endTime.si() == 2.0);
    CHECK(part.definition().transient->output == TransientOutputPolicy::EveryStep);
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().transient == transientOf(0.3, 1.0));   // 0.3 is the exact double it was, not 0.3 recomputed
    CHECK(part.definition().transient->timeStep.si() == 0.3);
    CHECK(part.definition().transient->output == TransientOutputPolicy::FinalOnly);
    CHECK(part.fingerprint() == before);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.fingerprint() == after);
}

TEST_CASE("ThermalCommand_TransientSettingsDoNotPersistDerivedTimeGrid", "[thermal][command][transient][derived]") {
    // 0.3 does not divide 1.0: a run takes 0.3, 0.3, 0.3 and a shortened remainder. That step SEQUENCE is derived by
    // P18-TRANSIENT; the canonical intent -- and so the command's whole payload -- is the three fields and nothing
    // else. The definition has no vector in which a grid could be stored, so two definitions that differ only in the
    // transient settings differ in exactly those three fields.
    CmdPart part{ThermalAnalysisMode::Transient};
    auto command = std::make_unique<SetTransientSettingsCommand>(part.analysis, transientOf(0.3, 1.0, TransientOutputPolicy::EveryStep));
    const SetTransientSettingsCommand* raw = command.get();
    REQUIRE(part.history.execute(part.document, std::move(command)).has_value());
    ThermalAnalysisDefinition expected = *raw->before();
    expected.transient = transientOf(0.3, 1.0, TransientOutputPolicy::EveryStep);
    CHECK(*raw->after() == expected);
    CHECK(raw->changedAnything());
}

TEST_CASE("ThermalCommand_InvalidTransientSettingsAtomic", "[thermal][command][transient][atomic]") {
    CmdPart part{ThermalAnalysisMode::Transient};
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.0, 1.0)).has_value());      // dt = 0
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(-0.1, 1.0)).has_value());     // dt < 0
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(kNaN, 1.0)).has_value());
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.1, kNaN)).has_value());
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.1, kInf)).has_value());
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(2.0, 1.0)).has_value());      // dt > span
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.1, -1.0)).has_value());
    // dt VALID, t_end INVALID: the dt must not land.
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.05, -3.0)).has_value());
    CHECK(snapshotOf(part) == before);
    CHECK(part.definition().transient->timeStep.si() == 0.3);
}

TEST_CASE("ThermalCommand_TransientSettingsAreRefusedForASteadyAnalysis", "[thermal][command][transient][mode]") {
    CmdPart part;   // steady
    const Snapshot before = snapshotOf(part);
    const Result<void> refused = part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.1, 1.0));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK(snapshotOf(part) == before);
    CHECK_FALSE(part.definition().transient.has_value());
}

// ===========================================================================
// Atomicity, history and redo
// ===========================================================================

TEST_CASE("ThermalCommand_FailedCommandDoesNotPushHistory", "[thermal][command][atomic][history]") {
    CmdPart part;
    CHECK_FALSE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    CHECK_FALSE(part.run<RemoveThermalBoundaryConditionCommand>(part.analysis, ThermalBoundaryConditionId::fromValue(1)).has_value());
    CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, kNaN)).has_value());
    CHECK(part.history.undoCount() == 0U);
    CHECK_FALSE(part.history.canUndo());
    CHECK_FALSE(part.history.canRedo());
}

TEST_CASE("ThermalCommand_FailedCommandDoesNotChangeSource", "[thermal][command][atomic][source]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 2.0)).has_value());
    CHECK_FALSE(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{.relativeResidualTolerance = -1.0}).has_value());
    CHECK_FALSE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(1.0, 2.0)).has_value());
    // The analysis revision AND the document revision are what a source comparison reads; neither moved.
    CHECK(part.revision() == before.analysisRevision);
    CHECK(part.document.revision() == before.documentRevision);
    CHECK(snapshotOf(part) == before);
}

TEST_CASE("ThermalCommand_UndoMakesRedoAvailable", "[thermal][command][redo]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 2.0)).has_value());
    CHECK_FALSE(part.history.canRedo());
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.history.canRedo());
    CHECK(part.history.redoCount() == 1U);
    CHECK(part.history.undoCount() == 1U);
}

TEST_CASE("ThermalCommand_RedoRedoLeavesNothingToRedo", "[thermal][command][redo]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK_FALSE(part.history.canRedo());
    CHECK_FALSE(part.history.redo(part.document).has_value());
    CHECK(part.definition().loads.size() == 1U);
}

TEST_CASE("ThermalCommand_SuccessfulBranchClearsRedo", "[thermal][command][redo]") {
    // execute A, execute B, undo B -> redo holds B; execute C -> redo is CLEARED and B cannot come back.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());      // A
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 2.0)).has_value());      // B
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.redoCount() == 1U);
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(3, 3.0)).has_value());      // C
    CHECK(part.history.redoCount() == 0U);
    CHECK_FALSE(part.history.redo(part.document).has_value());
    REQUIRE(part.definition().loads.size() == 2U);   // A and C: no ghost B
    CHECK(part.definition().loads[0].id() == ThermalLoadId::fromValue(1));
    CHECK(part.definition().loads[1].id() == ThermalLoadId::fromValue(3));
}

TEST_CASE("ThermalCommand_FailedBranchDoesNotClearRedo", "[thermal][command][redo][atomic]") {
    // The same setup, but C FAILS: the redo of B must survive, and the document must still be exactly "A only".
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 2.0)).has_value());
    REQUIRE(part.history.undo(part.document).has_value());
    const Snapshot before = snapshotOf(part);
    REQUIRE(before.redoDepth == 1U);

    CHECK_FALSE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(3, kNaN)).has_value());
    CHECK_FALSE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(9, 1.0)).has_value());
    CHECK(snapshotOf(part) == before);
    CHECK(part.history.canRedo());
    REQUIRE(part.history.redo(part.document).has_value());   // B is still there to come back
    CHECK(part.definition().loads.size() == 2U);
    CHECK(part.definition().loads[1].id() == ThermalLoadId::fromValue(2));
}

TEST_CASE("ThermalCommand_MultiLevelUndoRedo", "[thermal][command][history]") {
    CmdPart part;
    std::vector<std::uint64_t> states{part.fingerprint()};
    for (std::uint64_t id = 1; id <= 6; ++id) {
        REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(id, static_cast<double>(id) * 10.5)).has_value());
        states.push_back(part.fingerprint());
    }
    for (std::size_t step = states.size() - 1U; step > 0U; --step) {
        REQUIRE(part.fingerprint() == states[step]);
        REQUIRE(part.history.undo(part.document).has_value());
    }
    CHECK(part.fingerprint() == states[0]);
    for (std::size_t step = 1U; step < states.size(); ++step) {
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(part.fingerprint() == states[step]);
    }
}

TEST_CASE("ThermalCommand_HeterogeneousFullUndoReturnsBaseline", "[thermal][command][history][heterogeneous]") {
    // One of every command class, on a transient analysis. Each consecutive pair of states must DIFFER (a non-vacuity
    // gate: nine comparisons of one unchanging value would prove nothing), the full undo must return the baseline, and
    // the full redo must return the original final state, checked at EVERY step.
    CmdPart part{ThermalAnalysisMode::Transient};
    const std::uint64_t baseline = part.fingerprint();
    std::vector<std::uint64_t> states{baseline};
    const auto step = [&](auto&& runner) {
        REQUIRE(runner().has_value());
        states.push_back(part.fingerprint());
    };
    step([&] { return part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.endCap(), 1250.0)); });
    step([&] { return part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 4.0e4)); });
    step([&] { return part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 350.0)); });
    step([&] { return part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), 25.0, 300.0)); });
    step([&] { return part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(310.5)}); });
    step([&] { return part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{.relativeResidualTolerance = 2.0e-10, .pivotFloor = 1.0e-13}); });
    step([&] { return part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.125, 2.0, TransientOutputPolicy::EveryStep)); });
    step([&] { return part.run<EditThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(1), -300.0)); });
    step([&] { return part.run<EditThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 325.0)); });
    step([&] { return part.run<RemoveThermalBoundaryConditionCommand>(part.analysis, ThermalBoundaryConditionId::fromValue(2)); });
    step([&] { return part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(2)); });
    const std::uint64_t finalState = states.back();

    for (std::size_t i = 1; i < states.size(); ++i) {
        INFO("states " << (i - 1) << " and " << i);
        REQUIRE(states[i] != states[i - 1]);   // every command changed the intent
    }

    for (std::size_t i = states.size() - 1U; i > 0U; --i) {
        REQUIRE(part.fingerprint() == states[i]);
        REQUIRE(part.history.undo(part.document).has_value());
    }
    CHECK(part.fingerprint() == baseline);
    CHECK_FALSE(part.history.canUndo());
    for (std::size_t i = 1; i < states.size(); ++i) {
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(part.fingerprint() == states[i]);
    }
    CHECK(part.fingerprint() == finalState);
    CHECK_FALSE(part.history.canRedo());
    CHECK(part.history.undoCount() == states.size() - 1U);
}

TEST_CASE("ThermalCommand_BranchingHistoryHasNoGhostEdits", "[thermal][command][history][redo]") {
    // A, B, C; undo C; undo B; execute D. The redo of B and C is gone and the state is exactly A + D.
    CmdPart part{ThermalAnalysisMode::Transient};
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 10.0)).has_value());                               // A
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 350.0)).has_value());   // B
    REQUIRE(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.1, 1.0)).has_value());                           // C
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.redoCount() == 2U);
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(1, 20.0)).has_value());                              // D
    CHECK(part.history.redoCount() == 0U);
    CHECK_FALSE(part.history.redo(part.document).has_value());
    CHECK(part.definition().boundaryConditions.empty());                        // B did not come back
    CHECK(part.definition().transient == transientOf(0.3, 1.0));                // C did not come back
    CHECK(part.definition().loads.front().volumetricHeatSource()->generation.si() == 20.0);
}

TEST_CASE("ThermalCommand_NoOpEditSucceedsAndMovesNothing", "[thermal][command][noop]") {
    // THE PROJECT'S NO-OP POLICY (P15, P16, P17): the command SUCCEEDS, moves no revision (so invalidates nothing) and
    // is STILL RECORDED by CommandHistory, which records every command whose execute() succeeded.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), 100.0)).has_value());
    REQUIRE(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{}).has_value());   // the same defaults
    const Snapshot before = snapshotOf(part);

    auto same = std::make_unique<EditThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), 100.0));
    const EditThermalLoadCommand* raw = same.get();
    REQUIRE(part.history.execute(part.document, std::move(same)).has_value());
    CHECK_FALSE(raw->changedAnything());
    CHECK(part.revision() == before.analysisRevision);
    CHECK(part.document.revision() == before.documentRevision);
    CHECK(part.fingerprint() == before.intent);
    CHECK(part.history.undoCount() == before.undoDepth + 1U);   // recorded, as every successful command is
}

TEST_CASE("ThermalCommand_UndoMovesTheRevisionForwardRatherThanBack", "[thermal][command][undo][revision]") {
    // CONTENT is exact; IDENTITY is monotonic. The analysis revision after an undo is strictly greater than it was
    // before the undo, and not equal to where it started.
    CmdPart part;
    const std::uint64_t r0 = part.revision();
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    const std::uint64_t r1 = part.revision();
    CHECK(r1 > r0);
    REQUIRE(part.history.undo(part.document).has_value());
    const std::uint64_t r2 = part.revision();
    CHECK(r2 > r1);
    CHECK(r2 != r0);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.revision() > r2);
}

TEST_CASE("ThermalCommand_UndoAndRedoRefuseBeforeExecution", "[thermal][command][undo]") {
    // A command that was never executed has no before-state to restore. Without the check, undo() would assign a
    // default-constructed definition.
    CmdPart part;
    AddThermalLoadCommand never{part.analysis, generationLoad(1, 1.0)};
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(never.undo(part.document).has_value());
    CHECK_FALSE(never.redo(part.document).has_value());
    CreateThermalAnalysisCommand neverCreated{"X", ThermalAnalysisDefinition{.mesh = part.control}};
    CHECK_FALSE(neverCreated.undo(part.document).has_value());
    CHECK_FALSE(neverCreated.redo(part.document).has_value());
    CHECK(snapshotOf(part) == before);
}

TEST_CASE("ThermalCommand_TwoAnalysesDoNotConfuseTheirIds", "[thermal][command][id][scope]") {
    // The same ThermalLoadId in two analyses is two different loads: editing one leaves the other untouched, and a
    // command keyed on one analysis cannot reach the other.
    CmdPart part;
    auto second = std::make_unique<CreateThermalAnalysisCommand>("Second", ThermalAnalysisDefinition{.mesh = part.control});
    const CreateThermalAnalysisCommand* raw = second.get();
    REQUIRE(part.history.execute(part.document, std::move(second)).has_value());
    const ThermalAnalysisId other = raw->analysisId();
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(other, generationLoad(1, 2.0)).has_value());
    REQUIRE(part.run<EditThermalLoadCommand>(other, generationLoad(1, 22.0)).has_value());
    CHECK(part.definition().loads.front().volumetricHeatSource()->generation.si() == 1.0);
    const ThermalAnalysis* otherObject = thermal::findThermalAnalysis(part.document, other);
    REQUIRE(otherObject != nullptr);
    CHECK(otherObject->definition().loads.front().volumetricHeatSource()->generation.si() == 22.0);
    // A removal keyed on the first leaves the second's same-numbered load alone.
    REQUIRE(part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(1)).has_value());
    CHECK(thermal::findThermalAnalysis(part.document, other)->definition().loads.size() == 1U);
}

TEST_CASE("ThermalCommand_ACommandCannotReachAnObjectOfAnotherKind", "[thermal][command][id][scope]") {
    // The id of the extrude feature is a valid object id but not an analysis: found by a dynamic_cast, not trusted.
    CmdPart part;
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(part.run<AddThermalLoadCommand>(ThermalAnalysisId::fromValue(part.feature.value()), generationLoad(1, 1.0)).has_value());
    CHECK_FALSE(part.run<SetThermalSolverSettingsCommand>(ThermalAnalysisId::fromValue(part.control.value()), ThermalSolverSettings{}).has_value());
    CHECK(snapshotOf(part) == before);
}

// ===========================================================================
// Determinism and descriptions
// ===========================================================================

namespace {

struct Journey {
    std::vector<std::uint64_t> states{};
    std::vector<std::string> descriptions{};
    std::vector<int> codes{};
    std::size_t undoDepth = 0;
    std::size_t redoDepth = 0;
};

[[nodiscard]] Journey runJourney() {
    CmdPart part{ThermalAnalysisMode::Transient};
    Journey out;
    const auto record = [&](const Result<void>& result) {
        out.codes.push_back(result.has_value() ? -1 : static_cast<int>(result.error().code));
        out.states.push_back(part.fingerprint());
        out.descriptions.push_back(part.history.undoDescription().value_or("-"));
    };
    record(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.endCap(), 1250.0)));
    record(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(1, part.side(), 25.0, 300.0)));
    record(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, kNaN)));   // refused
    record(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(347.875)}));
    record(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.125, 2.0)));
    record(part.history.undo(part.document));
    record(part.history.undo(part.document));
    record(part.run<EditThermalLoadCommand>(part.analysis, surfaceLoad(1, part.endCap(), 900.0)));
    record(part.history.redo(part.document));   // refused: the branch cleared the redo
    out.undoDepth = part.history.undoCount();
    out.redoDepth = part.history.redoCount();
    return out;
}

} // namespace

TEST_CASE("ThermalCommand_RepeatedSequenceDeterministic", "[thermal][command][determinism]") {
    const Journey first = runJourney();
    for (int run = 0; run < 4; ++run) {
        const Journey again = runJourney();
        CHECK(again.states == first.states);
        CHECK(again.descriptions == first.descriptions);
        CHECK(again.codes == first.codes);
        CHECK(again.undoDepth == first.undoDepth);
        CHECK(again.redoDepth == first.redoDepth);
    }
    // The journey really exercised both a refusal and a cleared redo.
    CHECK(first.codes[2] != -1);       // the NaN load was refused
    CHECK(first.codes.back() != -1);   // the redo after the branch was refused
    CHECK(first.redoDepth == 0U);
    WARN(std::format("CMDFP journey {:016x} {:016x} {:016x} {:016x} {:016x} {:016x} {:016x} {:016x} {:016x} depth {} redo {}",
                     first.states[0], first.states[1], first.states[2], first.states[3], first.states[4], first.states[5],
                     first.states[6], first.states[7], first.states[8], first.undoDepth, first.redoDepth));
}

TEST_CASE("ThermalCommand_HistorySemanticFingerprintDeterministic", "[thermal][command][determinism]") {
    // The fingerprint is a function of the canonical content only: it does not see the document, the revision, an
    // address or an iteration order. Two separate documents built the same way agree, and a different edit differs.
    CmdPart a;
    CmdPart b;
    REQUIRE(a.run<AddThermalLoadCommand>(a.analysis, generationLoad(1, 5.5)).has_value());
    REQUIRE(b.run<AddThermalLoadCommand>(b.analysis, generationLoad(1, 5.5)).has_value());
    CHECK(a.fingerprint() == b.fingerprint());
    REQUIRE(b.run<EditThermalLoadCommand>(b.analysis, generationLoad(1, 5.5000000000000009)).has_value());   // one ulp
    CHECK(a.fingerprint() != b.fingerprint());
}

TEST_CASE("ThermalCommand_DescriptionsAreStable", "[thermal][command][determinism]") {
    // The text a menu shows is part of the contract of a command: deterministic, and carrying the identity and the value
    // that distinguish one edit from the next. All ten.
    CmdPart part;
    CHECK(CreateThermalAnalysisCommand{"Second", ThermalAnalysisDefinition{.mesh = part.control}}.description() ==
          "Create thermal analysis 'Second'");
    CHECK(AddThermalLoadCommand{part.analysis, generationLoad(4, 1.0)}.description() == "Add volumetric_heat_source load 4");
    CHECK(AddThermalLoadCommand{part.analysis, surfaceLoad(5, part.side(), 1.0)}.description() == "Add surface_heat_flux load 5");
    CHECK(EditThermalLoadCommand{part.analysis, generationLoad(4, 2.0)}.description() == "Edit load 4");
    CHECK(RemoveThermalLoadCommand{part.analysis, ThermalLoadId::fromValue(4)}.description() == "Remove load 4");
    CHECK(AddThermalBoundaryConditionCommand{part.analysis, heldTemperature(6, part.endCap(), 300.0)}.description() ==
          "Add prescribed_temperature boundary condition 6");
    CHECK(AddThermalBoundaryConditionCommand{part.analysis, convecting(7, part.endCap(), 5.0, 300.0)}.description() ==
          "Add convection boundary condition 7");
    CHECK(EditThermalBoundaryConditionCommand{part.analysis, heldTemperature(6, part.endCap(), 310.0)}.description() ==
          "Edit boundary condition 6");
    CHECK(RemoveThermalBoundaryConditionCommand{part.analysis, ThermalBoundaryConditionId::fromValue(6)}.description() ==
          "Remove boundary condition 6");
    CHECK(SetInitialTemperatureCommand{part.analysis, UniformInitialTemperature{Temperature::fromSi(325.5)}}.description() ==
          "Set initial temperature (325.5 K)");
    CHECK(SetThermalSolverSettingsCommand{part.analysis, ThermalSolverSettings{.relativeResidualTolerance = 1.0e-10, .pivotFloor = 1.0e-13}}
              .description() == "Set thermal solver settings (residual 1e-10, pivot floor 1e-13)");
    CHECK(SetTransientSettingsCommand{part.analysis, transientOf(0.25, 1.0, TransientOutputPolicy::EveryStep)}.description() ==
          "Set transient settings (step 0.25 s, end 1 s, every_step)");
}
// ===========================================================================
// Type safety (the compile-fail group carries the rest)
// ===========================================================================

TEST_CASE("ThermalCommand_StrongIdsNotInterchangeable", "[thermal][command][types]") {
    // A mesh handle, a DOF index or another thermal id is not an analysis, a load or a condition: none converts.
    static_assert(!std::is_constructible_v<AddThermalLoadCommand, meshing::NodeId, ThermalLoad>);
    static_assert(!std::is_constructible_v<AddThermalLoadCommand, meshing::ElementId, ThermalLoad>);
    static_assert(!std::is_constructible_v<AddThermalLoadCommand, thermal::ThermalDofIndex, ThermalLoad>);
    static_assert(!std::is_constructible_v<AddThermalLoadCommand, ThermalLoadId, ThermalLoad>);
    static_assert(!std::is_constructible_v<RemoveThermalLoadCommand, ThermalAnalysisId, ThermalBoundaryConditionId>);
    static_assert(!std::is_constructible_v<RemoveThermalLoadCommand, ThermalAnalysisId, meshing::NodeId>);
    static_assert(!std::is_constructible_v<RemoveThermalBoundaryConditionCommand, ThermalAnalysisId, ThermalLoadId>);
    static_assert(!std::is_constructible_v<RemoveThermalBoundaryConditionCommand, ThermalAnalysisId, thermal::ThermalDofIndex>);
    static_assert(!std::is_constructible_v<SetInitialTemperatureCommand, meshing::NodeId, UniformInitialTemperature>);
    static_assert(!std::is_constructible_v<SetThermalSolverSettingsCommand, meshing::ElementId, ThermalSolverSettings>);
    static_assert(!std::is_constructible_v<SetTransientSettingsCommand, thermal::ThermalDofIndex, TransientSettings>);
    // No raw integer stands in for an id.
    static_assert(!std::is_convertible_v<std::uint64_t, ThermalAnalysisId>);
    static_assert(!std::is_convertible_v<std::uint64_t, ThermalLoadId>);
    static_assert(!std::is_convertible_v<ThermalLoadId, ThermalBoundaryConditionId>);
    static_assert(!std::is_convertible_v<meshing::NodeId, ThermalLoadId>);
    // The intended spellings DO construct.
    static_assert(std::is_constructible_v<AddThermalLoadCommand, ThermalAnalysisId, ThermalLoad>);
    static_assert(std::is_constructible_v<RemoveThermalLoadCommand, ThermalAnalysisId, ThermalLoadId>);
    SUCCEED();
}

TEST_CASE("ThermalCommand_CommandsAreNotCopyable", "[thermal][command][types]") {
    // History must never come to hold two owners of one command.
    static_assert(!std::is_copy_constructible_v<AddThermalLoadCommand>);
    static_assert(!std::is_copy_constructible_v<CreateThermalAnalysisCommand>);
    static_assert(!std::is_copy_assignable_v<SetTransientSettingsCommand>);
    SUCCEED();
}

TEST_CASE("ThermalCommand_AFaceIsTheOnlyTargetAThermalRecordTakes", "[thermal][command][types][reference]") {
    // The canonical GeometryReference of a thermal load or condition is a FaceName. A mesh handle cannot be one.
    static_assert(!std::is_constructible_v<SurfaceHeatFluxLoad, meshing::NodeId, HeatFlux>);
    static_assert(!std::is_convertible_v<meshing::NodeId, FaceName>);
    static_assert(!std::is_convertible_v<meshing::ElementId, FaceName>);
    static_assert(!std::is_convertible_v<thermal::ThermalDofIndex, FaceName>);
    static_assert(std::is_same_v<decltype(PrescribedTemperature::face), FaceName>);
    static_assert(std::is_same_v<decltype(ConvectionBoundary::face), FaceName>);
    static_assert(std::is_same_v<decltype(SurfaceHeatFluxLoad::face), FaceName>);
    SUCCEED();
}

TEST_CASE("ThermalCommand_EveryEditCommandPayloadIsTwoCanonicalDefinitions", "[thermal][command][types][derived]") {
    // THE PAYLOAD OF EVERY EDIT IS A PAIR OF `ThermalAnalysisDefinition`s AND NOTHING ELSE: the type of the accessors says
    // so, and for each command the pair is shown equal to a before and an after the TEST built by hand. A definition holds
    // intent only (ThermalData_TheDefinitionHoldsOnlyIntent), so there is no field in which a matrix, a vector of
    // temperatures, a heat flux or a mesh handle could be kept.
    static_assert(std::is_same_v<decltype(std::declval<const ThermalAnalysisEditCommand&>().before()),
                                 const std::optional<ThermalAnalysisDefinition>&>);
    static_assert(std::is_same_v<decltype(std::declval<const ThermalAnalysisEditCommand&>().after()),
                                 const std::optional<ThermalAnalysisDefinition>&>);

    const auto seeded = [](CmdPart& part) {
        REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.endCap(), 500.0)).has_value());
        REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 2.0e4)).has_value());
        REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 350.0)).has_value());
        REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.side(), 25.0, 300.0)).has_value());
    };

    struct Case {
        const char* name;
        std::function<std::unique_ptr<ThermalAnalysisEditCommand>(const CmdPart&)> make;
        std::function<ThermalAnalysisDefinition(const CmdPart&, ThermalAnalysisDefinition)> expect;
    };
    const std::vector<Case> cases{
        {"add load",
         [](const CmdPart& p) { return std::make_unique<AddThermalLoadCommand>(p.analysis, generationLoad(3, 9.0)); },
         [](const CmdPart&, ThermalAnalysisDefinition d) { d.loads.push_back(generationLoad(3, 9.0)); return d; }},
        {"edit load",
         [](const CmdPart& p) { return std::make_unique<EditThermalLoadCommand>(p.analysis, surfaceLoad(1, p.side(2), -75.0)); },
         [](const CmdPart& p, ThermalAnalysisDefinition d) { d.loads[0] = surfaceLoad(1, p.side(2), -75.0); return d; }},
        {"remove load",
         [](const CmdPart& p) { return std::make_unique<RemoveThermalLoadCommand>(p.analysis, ThermalLoadId::fromValue(1)); },
         [](const CmdPart&, ThermalAnalysisDefinition d) { d.loads.erase(d.loads.begin()); return d; }},
        {"add condition",
         [](const CmdPart& p) { return std::make_unique<AddThermalBoundaryConditionCommand>(p.analysis, heldTemperature(3, p.endCap(), 280.0)); },
         [](const CmdPart& p, ThermalAnalysisDefinition d) { d.boundaryConditions.push_back(heldTemperature(3, p.endCap(), 280.0)); return d; }},
        {"edit condition",
         [](const CmdPart& p) { return std::make_unique<EditThermalBoundaryConditionCommand>(p.analysis, convecting(2, p.side(), 30.0, 310.0)); },
         [](const CmdPart& p, ThermalAnalysisDefinition d) { d.boundaryConditions[1] = convecting(2, p.side(), 30.0, 310.0); return d; }},
        {"remove condition",
         [](const CmdPart& p) { return std::make_unique<RemoveThermalBoundaryConditionCommand>(p.analysis, ThermalBoundaryConditionId::fromValue(1)); },
         [](const CmdPart&, ThermalAnalysisDefinition d) { d.boundaryConditions.erase(d.boundaryConditions.begin()); return d; }},
        {"initial temperature",
         [](const CmdPart& p) { return std::make_unique<SetInitialTemperatureCommand>(p.analysis, UniformInitialTemperature{Temperature::fromSi(333.5)}); },
         [](const CmdPart&, ThermalAnalysisDefinition d) { d.initialCondition = UniformInitialTemperature{Temperature::fromSi(333.5)}; return d; }},
        {"solver settings",
         [](const CmdPart& p) { return std::make_unique<SetThermalSolverSettingsCommand>(p.analysis, ThermalSolverSettings{.relativeResidualTolerance = 5.0e-10, .pivotFloor = 4.0e-13}); },
         [](const CmdPart&, ThermalAnalysisDefinition d) { d.solver = ThermalSolverSettings{.relativeResidualTolerance = 5.0e-10, .pivotFloor = 4.0e-13}; return d; }},
        {"transient settings",
         [](const CmdPart& p) { return std::make_unique<SetTransientSettingsCommand>(p.analysis, transientOf(0.05, 0.5, TransientOutputPolicy::EveryStep)); },
         [](const CmdPart&, ThermalAnalysisDefinition d) { d.transient = transientOf(0.05, 0.5, TransientOutputPolicy::EveryStep); return d; }},
    };
    for (const Case& c : cases) {
        INFO(c.name);
        CmdPart part{ThermalAnalysisMode::Transient};
        seeded(part);
        const ThermalAnalysisDefinition before = part.definition();
        const ThermalAnalysisDefinition expected = c.expect(part, before);
        REQUIRE(expected != before);
        std::unique_ptr<ThermalAnalysisEditCommand> command = c.make(part);
        const ThermalAnalysisEditCommand* raw = command.get();
        REQUIRE_FALSE(raw->before().has_value());   // nothing is held until the command has executed
        REQUIRE(part.history.execute(part.document, std::move(command)).has_value());
        REQUIRE(raw->before().has_value());
        REQUIRE(raw->after().has_value());
        CHECK(*raw->before() == before);
        CHECK(*raw->after() == expected);
        CHECK(part.definition() == expected);
        CHECK(raw->changedAnything());
        CHECK(raw->analysis() == part.analysis);
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(part.definition() == before);
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(part.definition() == expected);
    }
}

// ===========================================================================
// Findings of the independent adversarial review, each closed by a test that fails without the fix
// ===========================================================================

TEST_CASE("ThermalCommand_ATargetThatNamesNoFeatureIsRefused", "[thermal][command][reference][atomic]") {
    // The data model validates a face SELECTOR but lets through a FaceName whose feature is the invalid default handle. A
    // command refuses it at the boundary: it is not a canonical target representation, it could only be reported
    // unresolved, and a handle that is VALID and names a feature that does not exist is still accepted (a load may be
    // defined before any mesh exists).
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), 10.0)).has_value());
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 300.0)).has_value());
    const Snapshot before = snapshotOf(part);

    SECTION("a load") {
        const Result<void> refused = part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(2, FaceName{}, 5.0));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("an edit of a load") {
        CHECK_FALSE(part.run<EditThermalLoadCommand>(part.analysis, surfaceLoad(1, FaceName{}, 5.0)).has_value());
    }
    SECTION("a condition") {
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(2, FaceName{}, 300.0)).has_value());
        CHECK_FALSE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(3, FaceName{}, 5.0, 300.0)).has_value());
    }
    SECTION("an edit of a condition") {
        CHECK_FALSE(part.run<EditThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, FaceName{}, 300.0)).has_value());
    }
    SECTION("a creation that carries such a record") {
        ThermalAnalysisDefinition bad{.mesh = part.control};
        bad.loads.push_back(surfaceLoad(1, FaceName{}, 1.0));
        CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("Bad", bad).has_value());
        ThermalAnalysisDefinition badCondition{.mesh = part.control};
        badCondition.boundaryConditions.push_back(heldTemperature(1, FaceName{}, 300.0));
        CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("Bad2", badCondition).has_value());
    }
    CHECK(snapshotOf(part) == before);

    // ... and a valid handle that names nothing is accepted: the reference is preserved, unresolved.
    const FaceName ghost{ObjectId::fromValue(987654), FaceSelector{.role = FaceRole::EndCap}};
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(3, ghost, 1.0)).has_value());
}

TEST_CASE("ThermalCommand_ASignedZeroEditIsANoOpAndTheCommandRecordsWhatTheDocumentHolds", "[thermal][command][noop][exact]") {
    // The document's equality treats -0.0 and +0.0 as the same value, so changing a flux from +0.0 to -0.0 is not an edit:
    // setDefinition reports no change and moves no revision. The command must then record what the DOCUMENT holds -- not the
    // requested -0.0 -- so that after() agrees with the document and an undo/redo cannot disagree with it.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), 0.0)).has_value());
    const std::uint64_t revision = part.revision();
    auto edit = std::make_unique<EditThermalLoadCommand>(part.analysis, surfaceLoad(1, part.side(), -0.0));
    const EditThermalLoadCommand* raw = edit.get();
    REQUIRE(part.history.execute(part.document, std::move(edit)).has_value());
    CHECK_FALSE(raw->changedAnything());
    CHECK(part.revision() == revision);
    const auto bitsOf = [](const ThermalAnalysisDefinition& d) {
        return std::bit_cast<std::uint64_t>(d.loads.front().surfaceHeatFlux()->flux.si());
    };
    REQUIRE(raw->after().has_value());
    CHECK(bitsOf(*raw->after()) == bitsOf(part.definition()));   // after() IS what the document holds
    CHECK(bitsOf(part.definition()) == std::bit_cast<std::uint64_t>(0.0));   // the document kept +0.0
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.revision() == revision);   // a no-op round trip moves nothing
}

TEST_CASE("ThermalCommand_UndoRefusesWhenTheAnalysisWasChangedOutsideTheHistory", "[thermal][command][undo][redo][atomic]") {
    // CommandHistory undoes in reverse order, so the analysis holds exactly what the command installed. If something edited
    // it behind the history's back, assigning the stored definition would silently discard that change: the command refuses,
    // and the document, the command and both stacks stay as they were.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    const auto sneak = [&](double generation) {
        ThermalAnalysisDefinition changed = part.definition();
        changed.loads.push_back(generationLoad(9, generation));
        const Result<bool> done = part.document.modifyObject<ThermalAnalysis>(
            ObjectId::fromValue(part.analysis.value()),
            [&changed](ThermalAnalysis& object) { return object.setDefinition(changed); });
        REQUIRE(done.has_value());
    };
    sneak(7.0);
    const Snapshot outside = snapshotOf(part);
    const Result<void> refused = part.history.undo(part.document);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("outside the command history"));
    CHECK(snapshotOf(part) == outside);          // nothing moved: the outside edit survives, the stacks are intact
    CHECK(part.definition().loads.size() == 2U);

    // Put the document back where the command expects it, and the same undo now works.
    ThermalAnalysisDefinition restored = part.definition();
    restored.loads.pop_back();
    REQUIRE(part.document.modifyObject<ThermalAnalysis>(ObjectId::fromValue(part.analysis.value()),
                                                        [&restored](ThermalAnalysis& object) { return object.setDefinition(restored); })
                .has_value());
    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(part.definition().loads.empty());

    // The same for redo.
    ThermalAnalysisDefinition other = part.definition();
    other.loads.push_back(generationLoad(8, 3.0));
    REQUIRE(part.document.modifyObject<ThermalAnalysis>(ObjectId::fromValue(part.analysis.value()),
                                                        [&other](ThermalAnalysis& object) { return object.setDefinition(other); })
                .has_value());
    const Snapshot beforeRedo = snapshotOf(part);
    CHECK_FALSE(part.history.redo(part.document).has_value());
    CHECK(snapshotOf(part) == beforeRedo);
    CHECK(part.history.canRedo());
}

TEST_CASE("ThermalCommand_UndoAndRedoRefuseWhenTheAnalysisIsGone", "[thermal][command][undo][redo][atomic]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, 2.0)).has_value());
    REQUIRE(part.history.undo(part.document).has_value());   // redo now holds the second add
    REQUIRE(part.document.removeObject(ObjectId::fromValue(part.analysis.value())).has_value());   // out of band
    const std::size_t objects = part.document.objectCount();
    const std::size_t undoDepth = part.history.undoCount();
    const std::size_t redoDepth = part.history.redoCount();

    const Result<void> undone = part.history.undo(part.document);
    REQUIRE_FALSE(undone.has_value());
    CHECK(undone.error().code == ErrorCode::NotFound);
    const Result<void> redone = part.history.redo(part.document);
    REQUIRE_FALSE(redone.has_value());
    CHECK(redone.error().code == ErrorCode::NotFound);
    CHECK(part.document.objectCount() == objects);
    CHECK(part.history.undoCount() == undoDepth);   // the commands stay where they were
    CHECK(part.history.redoCount() == redoDepth);
}

TEST_CASE("ThermalCommand_CreateThenDependentCommandsUndoRedoEverything", "[thermal][command][create][history]") {
    // A created analysis is edited, and the whole history is undone and redone: the dependent commands find the analysis
    // by its id, which redo of the creation restored.
    CmdPart part;
    const std::size_t objects = part.document.objectCount();
    ThermalAnalysisDefinition seed{.mesh = part.control, .mode = ThermalAnalysisMode::Transient};
    seed.initialCondition = UniformInitialTemperature{Temperature::fromSi(300.0)};
    seed.transient = transientOf(0.3, 1.0);
    auto create = std::make_unique<CreateThermalAnalysisCommand>("Transient", seed);
    const CreateThermalAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());
    const ThermalAnalysisId made = raw->analysisId();
    REQUIRE(part.run<AddThermalLoadCommand>(made, surfaceLoad(1, part.endCap(), 250.0)).has_value());
    REQUIRE(part.run<SetInitialTemperatureCommand>(made, UniformInitialTemperature{Temperature::fromSi(310.25)}).has_value());
    REQUIRE(part.run<SetTransientSettingsCommand>(made, transientOf(0.125, 2.0, TransientOutputPolicy::EveryStep)).has_value());
    const auto fingerprint = [&] { return fingerprintOf(thermal::findThermalAnalysis(part.document, made)->definition()); };
    const std::uint64_t final = fingerprint();

    for (int i = 0; i < 4; ++i) {
        REQUIRE(part.history.undo(part.document).has_value());
    }
    CHECK(part.document.objectCount() == objects);
    CHECK(thermal::findThermalAnalysis(part.document, made) == nullptr);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(part.history.redo(part.document).has_value());
    }
    CHECK(raw->analysisId() == made);
    CHECK(fingerprint() == final);
    CHECK(thermal::findThermalAnalysis(part.document, made)->definition().transient == transientOf(0.125, 2.0, TransientOutputPolicy::EveryStep));
}

TEST_CASE("ThermalCommand_CreateKeepsTheMeshHandleAsGiven", "[thermal][command][create][reference]") {
    // The mesh-control handle is canonical intent like a FaceName: a command does not resolve it, so one that names nothing
    // is created and reported by the model gate (`requireThermalModel`) when the analysis is prepared. The invalid default
    // handle and an invalid name are refused.
    CmdPart part;
    const MeshControlId dangling = MeshControlId::fromValue(987654);
    auto create = std::make_unique<CreateThermalAnalysisCommand>("Dangling", ThermalAnalysisDefinition{.mesh = dangling});
    const CreateThermalAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());
    CHECK(thermal::findThermalAnalysis(part.document, raw->analysisId())->definition().mesh == dangling);
    const Snapshot before = snapshotOf(part);
    CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("1 not a name", ThermalAnalysisDefinition{.mesh = part.control}).has_value());
    CHECK_FALSE(part.run<CreateThermalAnalysisCommand>("", ThermalAnalysisDefinition{.mesh = part.control}).has_value());
    CHECK(snapshotOf(part) == before);
}

TEST_CASE("ThermalCommand_AnUndoOrRedoOfANoOpMovesNothing", "[thermal][command][noop][undo]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 5.0)).has_value());
    REQUIRE(part.run<EditThermalLoadCommand>(part.analysis, generationLoad(1, 5.0)).has_value());   // changes nothing
    REQUIRE(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{}).has_value());   // changes nothing
    const std::uint64_t revision = part.revision();
    const std::uint64_t intent = part.fingerprint();
    REQUIRE(part.history.undo(part.document).has_value());   // the no-op solver settings
    CHECK(part.revision() == revision);
    REQUIRE(part.history.undo(part.document).has_value());   // the no-op edit
    CHECK(part.revision() == revision);
    CHECK(part.fingerprint() == intent);
    REQUIRE(part.history.redo(part.document).has_value());
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.revision() == revision);
    CHECK(part.definition().loads.size() == 1U);
}

TEST_CASE("ThermalCommand_RefusalsCarryStableCodes", "[thermal][command][atomic][diagnostics]") {
    CmdPart part;   // steady
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    REQUIRE(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.startCap(), 300.0)).has_value());
    const auto code = [](const Result<void>& result) { return result.has_value() ? ErrorCode{} : result.error().code; };

    CHECK(code(part.run<RemoveThermalLoadCommand>(part.analysis, ThermalLoadId::fromValue(9))) == ErrorCode::NotFound);
    CHECK(code(part.run<EditThermalBoundaryConditionCommand>(part.analysis, heldTemperature(9, part.endCap(), 300.0))) == ErrorCode::NotFound);
    CHECK(code(part.run<RemoveThermalBoundaryConditionCommand>(part.analysis, ThermalBoundaryConditionId::fromValue(9))) == ErrorCode::NotFound);
    CHECK(code(part.run<AddThermalLoadCommand>(ThermalAnalysisId::fromValue(987654), generationLoad(2, 1.0))) == ErrorCode::NotFound);
    CHECK(code(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 2.0))) == ErrorCode::AlreadyExists);
    CHECK(code(part.run<AddThermalBoundaryConditionCommand>(part.analysis, heldTemperature(1, part.endCap(), 300.0))) == ErrorCode::AlreadyExists);
    CHECK(code(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(2, kNaN))) == ErrorCode::InvalidArgument);
    CHECK(code(part.run<AddThermalBoundaryConditionCommand>(part.analysis, convecting(2, part.endCap(), -1.0, 300.0))) == ErrorCode::InvalidArgument);
    CHECK(code(part.run<SetThermalSolverSettingsCommand>(part.analysis, ThermalSolverSettings{.relativeResidualTolerance = kNaN})) == ErrorCode::InvalidArgument);
    // A record that is BOTH invalid and a duplicate reports its own defect first, deterministically.
    CHECK(code(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, kNaN))) == ErrorCode::InvalidArgument);
    // THE MODE COMES FIRST: a steady analysis cannot take the field, whatever the value, so a NaN is no different.
    CHECK(code(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(300.0)})) == ErrorCode::FailedPrecondition);
    CHECK(code(part.run<SetInitialTemperatureCommand>(part.analysis, UniformInitialTemperature{Temperature::fromSi(kNaN)})) == ErrorCode::FailedPrecondition);
    CHECK(code(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(0.1, 1.0))) == ErrorCode::FailedPrecondition);
    CHECK(code(part.run<SetTransientSettingsCommand>(part.analysis, transientOf(kNaN, 1.0))) == ErrorCode::FailedPrecondition);
    // On a transient analysis the value is judged.
    CmdPart transient{ThermalAnalysisMode::Transient};
    CHECK(code(transient.run<SetInitialTemperatureCommand>(transient.analysis, UniformInitialTemperature{Temperature::fromSi(kNaN)})) == ErrorCode::InvalidArgument);
    CHECK(code(transient.run<SetTransientSettingsCommand>(transient.analysis, transientOf(kNaN, 1.0))) == ErrorCode::InvalidArgument);
}

TEST_CASE("ThermalCommand_ARefusedCommandIsLeftUnexecuted", "[thermal][command][atomic]") {
    // CommandHistory discards a failed command, so a test through it never sees the command again. Driven directly, the
    // command is observed: nothing was recorded, nothing changed, and it can neither be undone nor redone.
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    const Snapshot before = snapshotOf(part);
    AddThermalLoadCommand duplicate{part.analysis, generationLoad(1, 2.0)};
    REQUIRE_FALSE(duplicate.execute(part.document).has_value());
    CHECK_FALSE(duplicate.before().has_value());
    CHECK_FALSE(duplicate.after().has_value());
    CHECK_FALSE(duplicate.changedAnything());
    CHECK_FALSE(duplicate.undo(part.document).has_value());
    CHECK_FALSE(duplicate.redo(part.document).has_value());
    CHECK(snapshotOf(part) == before);
}

namespace {

/// A command whose own checks pass and whose result the DOCUMENT refuses: the definition it derives carries the invalid
/// mesh-control handle. Every real edit validates what it changes before it derives a definition, so the last line of
/// defence -- `setDefinition` refusing an apply -- cannot be reached through them; the edit base is the thing under
/// test here, and a derived class is the only honest way to reach it.
class RefusedByTheDocument final : public ThermalAnalysisEditCommand {
public:
    explicit RefusedByTheDocument(ThermalAnalysisId analysis) noexcept : ThermalAnalysisEditCommand(analysis) {}
    [[nodiscard]] std::string description() const override { return "A definition the document refuses"; }

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition> edit(const ThermalAnalysisDefinition& before) const override {
        ThermalAnalysisDefinition after = before;
        after.mesh = MeshControlId{};
        return after;
    }
};

} // namespace

TEST_CASE("ThermalCommand_ADefinitionTheDocumentRefusesLeavesTheCommandUnexecuted", "[thermal][command][atomic]") {
    CmdPart part;
    REQUIRE(part.run<AddThermalLoadCommand>(part.analysis, generationLoad(1, 1.0)).has_value());
    const Snapshot before = snapshotOf(part);

    // Through the history: refused, nothing recorded, nothing cleared.
    REQUIRE(part.history.undo(part.document).has_value());
    const Snapshot withRedo = snapshotOf(part);
    CHECK(withRedo.redoDepth == 1U);
    CHECK_FALSE(part.history.execute(part.document, std::make_unique<RefusedByTheDocument>(part.analysis)).has_value());
    CHECK(snapshotOf(part) == withRedo);   // the redo entry survived a REFUSED command
    REQUIRE(part.history.redo(part.document).has_value());
    const Snapshot settled = snapshotOf(part);   // the undo and redo moved the revisions forward, as they always do
    CHECK(settled.undoDepth == before.undoDepth);
    CHECK(settled.redoDepth == 0U);
    CHECK(part.definition().loads.size() == 1U);

    // Driven directly: the command is observed, and it is as unexecuted as one that was never run.
    RefusedByTheDocument refused{part.analysis};
    const Result<void> result = refused.execute(part.document);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    CHECK_FALSE(refused.before().has_value());   // the staged state was taken back
    CHECK_FALSE(refused.after().has_value());
    CHECK_FALSE(refused.changedAnything());
    CHECK_FALSE(refused.undo(part.document).has_value());
    CHECK_FALSE(refused.redo(part.document).has_value());
    CHECK(snapshotOf(part) == settled);
}
