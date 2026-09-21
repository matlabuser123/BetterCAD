#include "cli/CliRunner.hpp"
#include "reference/AssemblyTestSupport.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/assembly/Commands.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <set>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::asmref;
using bettercad::cli::ExitCode;
using reference::AssemblyReferenceModelKind;

namespace {
constexpr std::array<double, 3> kUpZ{0.0, 0.0, 1.0};
}
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P13-REFMOD-001: what the committed assembly models have to survive.
//
// The models themselves are checked in AssemblyModelsTests.cpp. This file is
// about the WORKFLOWS: that the committed files are the builders' own output,
// that regenerating and solving one twice gives the same answer, that a save
// and a load change nothing, that the CLI drives them, and that a STEP export
// read back through an independent reader contains what the solve said it
// would.
namespace {

/// The committed models live beside the part models, which is where every
/// other reference asset already is.
[[nodiscard]] std::filesystem::path referenceDirectory() {
    return std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "reference";
}

[[nodiscard]] std::filesystem::path committedPath(const reference::AssemblyReferenceModelInfo& info) {
    return referenceDirectory() / (std::string{info.fileStem} + ".bcad");
}

constexpr double kPi = std::numbers::pi;

/// What a STEP export of each model must contain, derived from the parts the
/// builders were given and the components that place them -- never from the
/// exporter.
///
/// `volumeMm3` is the SUM of the instances' volumes, because STEP writes each
/// instance as its own solid; overlapping solids are not unioned.
struct StepExpectation {
    AssemblyReferenceModelKind kind;
    std::size_t products;
    std::size_t instances;
    double volumeMm3;
};

const std::array kStepExpected{
    // Base 80x60x10 + Cover 80x60x6.
    StepExpectation{AssemblyReferenceModelKind::GroundedPair, 2, 2, 80.0 * 60.0 * 10.0 + 80.0 * 60.0 * 6.0},
    // Deck 100x80x12, and the Arm 40x30x10 written once but placed twice.
    StepExpectation{AssemblyReferenceModelKind::ConstrainedStack, 2, 3,
                    100.0 * 80.0 * 12.0 + 2.0 * 40.0 * 30.0 * 10.0},
    // Housing 60x60x40 + a shaft of radius 10, 80 long.
    StepExpectation{AssemblyReferenceModelKind::ShaftInBore, 2, 2,
                    60.0 * 60.0 * 40.0 + kPi * 10.0 * 10.0 * 80.0},
    // Frame, Link, Shoe, Sleeve (a disc), Pad.
    StepExpectation{AssemblyReferenceModelKind::JointSet, 5, 5,
                    120.0 * 100.0 * 12.0 + 50.0 * 12.0 * 8.0 + 30.0 * 20.0 * 10.0 + kPi * 8.0 * 8.0 * 40.0 +
                        40.0 * 40.0 * 6.0},
    // Bed, two Posts of one part, Brace.
    StepExpectation{AssemblyReferenceModelKind::ConfiguredFrame, 3, 4,
                    100.0 * 60.0 * 14.0 + 2.0 * 15.0 * 15.0 * 50.0 + 70.0 * 10.0 * 8.0},
    // Body 80x60x30 + Lid 80x60x8.
    StepExpectation{AssemblyReferenceModelKind::DrivenCover, 2, 2, 80.0 * 60.0 * 30.0 + 80.0 * 60.0 * 8.0},
    // Housing, Cover, two Shafts of one part, four Feet of one part.
    StepExpectation{AssemblyReferenceModelKind::Machine, 4, 8,
                    160.0 * 120.0 * 40.0 + 160.0 * 120.0 * 10.0 + 2.0 * kPi * 12.0 * 12.0 * 140.0 +
                        4.0 * 24.0 * 24.0 * 10.0},
};

[[nodiscard]] const StepExpectation* stepExpectationFor(AssemblyReferenceModelKind kind) {
    const auto found = std::ranges::find(kStepExpected, kind, &StepExpectation::kind);
    return found == kStepExpected.end() ? nullptr : &*found;
}

/// Every component's solved place, by name: what two runs have to agree on.
[[nodiscard]] std::map<std::string, std::array<double, 3>> placements(const Assembled& built) {
    std::map<std::string, std::array<double, 3>> places;
    for (const ComponentId id : assembly::activeComponents(built.document())) {
        const assembly::Component* component = assembly::findComponent(built.document(), id);
        const RigidTransform3D* motion = built.transform(id);
        REQUIRE(component != nullptr);
        REQUIRE(motion != nullptr);
        places.emplace(component->name(), positionMm(*motion));
    }
    return places;
}

} // namespace

// --- The committed files are the builders' own output ---------------------------

TEST_CASE("AssemblyReference_CommittedFilesMatchTheirBuilders", "[reference][assembly][p13][io]") {
    // examples/models/reference/assembly_*.bcad are what the builders make,
    // saved before regeneration, exactly as the part models are. They are in
    // the repository so the CLI and other tools have real assemblies to work
    // on, and they must stay in step: saving a freshly built model reproduces
    // its file byte for byte, and loading the file gives the same model back.
    TempDir dir;
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        INFO("model " << info.label << " " << info.name);
        const auto committed = committedPath(info);
        REQUIRE(std::filesystem::exists(committed));

        Document built = build(info.kind);
        const auto path = dir.path() / (std::string{info.fileStem} + ".bcad");
        REQUIRE(io::saveDocument(built, path).has_value());
        CHECK(readFile(path) == readFile(committed));

        auto loaded = io::loadDocument(committed);
        REQUIRE(loaded.has_value());
        CHECK(equivalent(*loaded, built));
    }
}

TEST_CASE("AssemblyReference_NoCommittedAssemblyIsAStrayFile", "[reference][assembly][p13][io]") {
    // The other direction: an assembly_*.bcad in the repository that no
    // builder claims would be an asset nothing tests and nothing maintains.
    std::set<std::string> expected;
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        expected.insert(std::string{info.fileStem} + ".bcad");
    }
    std::size_t seen = 0;
    for (const auto& entry : std::filesystem::directory_iterator(referenceDirectory())) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("assembly_")) {
            INFO("file " << name);
            CHECK(expected.contains(name));
            ++seen;
        }
    }
    // Without this the loop above would pass on an empty directory, which is
    // the one result it must never report as success.
    CHECK(seen == reference::kAssemblyReferenceModels.size());
}

// --- Determinism ----------------------------------------------------------------

TEST_CASE("AssemblyReference_RegeneratingAndSolvingTwiceGivesTheSameAnswer",
          "[reference][assembly][p13]") {
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        if (!info.solves) {
            continue;
        }
        INFO("model " << info.label);
        Assembled first{build(info.kind)};
        Assembled second{build(info.kind)};
        REQUIRE(first.regenerated());
        REQUIRE(second.regenerated());

        const auto a = first.solve();
        const auto b = second.solve();
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(a->status == b->status);
        CHECK(a->unknowns == b->unknowns);
        CHECK(a->equations == b->equations);
        CHECK(a->degreesOfFreedom == b->degreesOfFreedom);
        CHECK(a->iterations == b->iterations);
        // Bit for bit. The same intent solved twice is the same arithmetic,
        // and P13-PERSIST-001 already holds transforms to this standard, so
        // anything looser here would be a weaker claim than the one already
        // qualified.
        CHECK(a->maxResidual.si() == b->maxResidual.si());
        const auto placesA = placements(first);
        const auto placesB = placements(second);
        REQUIRE(placesA.size() == placesB.size());
        for (const auto& [name, place] : placesA) {
            INFO("component " << name);
            REQUIRE(placesB.contains(name));
            for (std::size_t i = 0; i < 3; ++i) {
                CHECK(placesB.at(name)[i] == place[i]);
            }
        }
    }
}

// --- Save, load, regenerate, solve ----------------------------------------------

TEST_CASE("AssemblyReference_SurvivesSaveLoadRegenerateSolve", "[reference][assembly][p13][io]") {
    // The round trip the gate names, on every model, from the COMMITTED file
    // rather than from a freshly built one: that is the artefact a user
    // actually opens.
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        if (!info.solves) {
            continue;
        }
        INFO("model " << info.label);
        TempDir dir;
        Assembled fromBuilder{build(info.kind)};
        REQUIRE(fromBuilder.regenerated());

        auto loaded = io::loadDocument(committedPath(info));
        REQUIRE(loaded.has_value());
        // Save what was loaded, load that, and solve it: intent must survive
        // two trips through the file unchanged.
        const auto again = dir.path() / "again.bcad";
        REQUIRE(io::saveDocument(*loaded, again).has_value());
        CHECK(readFile(again) == readFile(committedPath(info)));
        auto reloaded = io::loadDocument(again);
        REQUIRE(reloaded.has_value());

        Assembled fromFile{std::move(*reloaded)};
        REQUIRE(fromFile.regenerated());
        const auto built = fromBuilder.solve();
        const auto file = fromFile.solve();
        REQUIRE(built.has_value());
        REQUIRE(file.has_value());
        CHECK(file->status == built->status);
        CHECK(file->degreesOfFreedom == built->degreesOfFreedom);
        CHECK(file->equations == built->equations);

        // Derived transforms are recomputed, not stored -- and they come back
        // identical.
        const auto placesBuilt = placements(fromBuilder);
        const auto placesFile = placements(fromFile);
        REQUIRE(placesBuilt.size() == placesFile.size());
        for (const auto& [name, place] : placesBuilt) {
            INFO("component " << name);
            REQUIRE(placesFile.contains(name));
            for (std::size_t i = 0; i < 3; ++i) {
                CHECK(placesFile.at(name)[i] == place[i]);
            }
        }
    }
}

// --- STEP export and read-back ---------------------------------------------------

TEST_CASE("AssemblyReference_ExportsToStepAndReadsBackWhatTheSolveSaid",
          "[reference][assembly][p13][io][step]") {
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        const StepExpectation* want = stepExpectationFor(info.kind);
        if (want == nullptr) {
            continue;
        }
        INFO("model " << info.label << " " << info.name);
        TempDir dir;
        auto loaded = io::loadDocument(committedPath(info));
        REQUIRE(loaded.has_value());

        const auto path = dir.path() / (std::string{info.fileStem} + ".step");
        const auto summary = io::exportStep(*loaded, path);
        INFO((summary ? std::string{} : summary.error().message));
        REQUIRE(summary.has_value());
        CHECK(summary->bodies.size() == want->products);
        CHECK(summary->components.size() == want->instances);

        // Read it back through STEPCAFControl_Reader, which shares no code
        // with the writer.
        const auto structure = readStepStructure(path);
        REQUIRE(structure.has_value());
        CHECK(structure->isAssembly);
        CHECK(structure->valid);
        CHECK(structure->name == info.name);
        CHECK(structure->products.size() == want->products);
        REQUIRE(structure->instances.size() == want->instances);

        // Every active component is in the file, by name, once.
        std::multiset<std::string> readBack;
        for (const test::StepShape& instance : structure->instances) {
            readBack.insert(instance.name);
        }
        Assembled built{std::move(*loaded)};
        REQUIRE(built.regenerated());
        for (const ComponentId id : assembly::activeComponents(built.document())) {
            const assembly::Component* component = assembly::findComponent(built.document(), id);
            REQUIRE(component != nullptr);
            INFO("component " << component->name());
            CHECK(readBack.count(component->name()) == 1);

            // And it is WHERE the solve put it. The bounding boxes of a part
            // and its instance differ by the placement, so the instance's
            // centre must sit within its own bounds and those bounds must
            // move with the component.
            const RigidTransform3D* motion = built.transform(id);
            REQUIRE(motion != nullptr);
            const auto found = std::ranges::find(structure->instances, component->name(), &test::StepShape::name);
            REQUIRE(found != structure->instances.end());
            const std::array<double, 3> place = positionMm(*motion);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                INFO("axis " << axis << " origin " << place[axis] << " bounds " << found->minMm[axis] << " .. "
                             << found->maxMm[axis]);
                // Every part is built from the origin of its own frame into
                // +X, +Y, +Z (a disc is centred in X and Y), so a component's
                // solved origin lies within its instance's bounding box.
                CHECK(place[axis] >= found->minMm[axis] - 1e-6);
                CHECK(place[axis] <= found->maxMm[axis] + 1e-6);
            }
            // Containment on its own is a weak claim -- a misplaced component
            // carries its own bounding box with it. The sharp one is
            // available on Z for every part in the suite, block or disc,
            // because both extrude from the origin in +Z: the instance's
            // LOWEST z in the file must BE the component's solved z. That
            // compares the exporter's answer with the solver's, so a transform
            // applied differently in the two would not survive it.
            checkDirection(axisMm(*motion), kUpZ);
            INFO("z: solved " << place[2] << ", file " << found->minMm[2]);
            CHECK_THAT(found->minMm[2], WithinAbs(place[2], 1e-6));
        }

        // The whole file holds exactly the material the parts add up to.
        const auto whole = readStepFile(path);
        REQUIRE(whole.has_value());
        CHECK_THAT(whole->volumeMm3, WithinRel(want->volumeMm3, 1e-9));
    }
}

TEST_CASE("AssemblyReference_TheUnsolvableModelIsRefusedRatherThanExported",
          "[reference][assembly][p13][io][step]") {
    // RM-H has no solved placement to write, so there is no honest file to
    // produce. Exporting the parts where their intent happened to put them
    // would be exporting an assembly that was never assembled.
    TempDir dir;
    auto loaded = io::loadDocument(referenceDirectory() / "assembly_fault_cases.bcad");
    REQUIRE(loaded.has_value());
    const auto path = dir.path() / "fault.step";
    const auto summary = io::exportStep(*loaded, path);
    REQUIRE_FALSE(summary.has_value());
    CHECK(summary.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(summary.error().message, ContainsSubstring("solve"));
    CHECK_FALSE(std::filesystem::exists(path));
}

// --- The CLI, on the committed files ---------------------------------------------

TEST_CASE("AssemblyReference_TheCliDrivesEveryCommittedModel", "[reference][assembly][p13][cli]") {
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        INFO("model " << info.label << " " << info.name);
        const std::string file = cliPath(committedPath(info));

        const auto status = runCliCommand({"status", file});
        // `status` reports the whole assembly whatever state it is in, but
        // its exit code follows the document: non-zero when the assembly
        // does not solve, so a script driver is not told all is well.
        CHECK(status.exitCode == (info.solves ? ExitCode::Success : ExitCode::Failure));
        CHECK_THAT(status.out, ContainsSubstring("Assembly of " + std::string{info.name}));
        CHECK_THAT(status.out, ContainsSubstring("Regeneration: ok"));

        // Regeneration succeeds for every model, including the one that does
        // not solve: a contradictory assembly is a solver outcome, and the
        // two must stay distinguishable at the command line too.
        const auto regenerate = runCliCommand({"regenerate", file});
        CHECK(regenerate.exitCode == ExitCode::Success);

        const auto solve = runCliCommand({"solve", file});
        if (info.solves) {
            // A solved assembly reports its status and every transform.
            CHECK(solve.exitCode == ExitCode::Success);
            CHECK_THAT(solve.out, ContainsSubstring("Transforms"));
        } else {
            // An inconsistent one is a FAILURE at the command line, so a
            // script driver stops rather than carrying on with nothing.
            CHECK(solve.exitCode == ExitCode::Failure);
            CHECK_THAT(solve.out, ContainsSubstring("INCONSISTENT"));
            CHECK_THAT(solve.out, ContainsSubstring("conflicting"));
        }
    }
}

TEST_CASE("AssemblyReference_TheCliExportsWhatTheCoreApiExports", "[reference][assembly][p13][cli][step]") {
    // The CLI must not have export semantics of its own. Compared as
    // geometry rather than as bytes, because the STEP header carries a
    // timestamp and an occurrence counter (P13-STEP-001).
    TempDir dir;
    const auto info = std::ranges::find(reference::kAssemblyReferenceModels, AssemblyReferenceModelKind::Machine,
                                        &reference::AssemblyReferenceModelInfo::kind);
    REQUIRE(info != reference::kAssemblyReferenceModels.end());
    auto loaded = io::loadDocument(committedPath(*info));
    REQUIRE(loaded.has_value());

    const auto viaCore = dir.path() / "core.step";
    REQUIRE(io::exportStep(*loaded, viaCore).has_value());
    const auto viaCli = dir.path() / "cli.step";
    const auto run = runCliCommand({"export-step", cliPath(committedPath(*info)), cliPath(viaCli)});
    REQUIRE(run.exitCode == ExitCode::Success);

    const auto core = readStepStructure(viaCore);
    const auto cli = readStepStructure(viaCli);
    REQUIRE(core.has_value());
    REQUIRE(cli.has_value());
    CHECK(core->products == cli->products);
    REQUIRE(core->instances.size() == cli->instances.size());
    for (std::size_t i = 0; i < core->instances.size(); ++i) {
        INFO("instance " << core->instances[i].name);
        CHECK(core->instances[i].name == cli->instances[i].name);
        CHECK_THAT(cli->instances[i].volumeMm3, WithinRel(core->instances[i].volumeMm3, 1e-12));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(cli->instances[i].minMm[axis], WithinAbs(core->instances[i].minMm[axis], 1e-12));
            CHECK_THAT(cli->instances[i].maxMm[axis], WithinAbs(core->instances[i].maxMm[axis], 1e-12));
        }
    }
}

// --- Gaps the adversarial review found ------------------------------------------

TEST_CASE("AssemblyReference_AParameterDrivenPlacementMovesTheAssembly",
          "[reference][assembly][p13]") {
    // P13-XFORM-001 lets a placement's translation be driven by a parameter
    // rather than a literal. Nothing in the suite showed that until RM-C's
    // shaft was bound this way, and nothing else could: a component held by
    // mates has its placement overwritten, so the binding is invisible. The
    // concentric mate leaves the slide along the bore free, so here the
    // parameter IS the answer.
    using M = reference::ShaftInBoreModel;
    auto model = reference::buildShaftInBoreReferenceModel();
    REQUIRE(model.has_value());
    const ParameterId along = model->shaftAlongBore;
    const ComponentId shaft = model->shaft;
    Document document = std::move(model->document);
    REQUIRE(along.isValid());

    const auto shaftHeight = [&]() {
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        const RigidTransform3D* motion = built.transform(shaft);
        REQUIRE(motion != nullptr);
        // On the axis whatever the parameter says, and still spun by the
        // angle it was placed with.
        CHECK_THAT(positionMm(*motion)[0], WithinAbs(0.0, kPlaceMm));
        CHECK_THAT(positionMm(*motion)[1], WithinAbs(0.0, kPlaceMm));
        checkDirection(rollMm(*motion), spin(M::kSpinDeg));
        return positionMm(*motion)[2];
    };

    CHECK_THAT(shaftHeight(), WithinAbs(M::kAlongZMm, kPlaceMm));
    REQUIRE(document.setParameterValue(along, 61.5_mm).has_value());
    CHECK_THAT(shaftHeight(), WithinAbs(61.5, kPlaceMm));
    REQUIRE(document.setParameterValue(along, M::kAlongZMm * units::mm).has_value());
    CHECK_THAT(shaftHeight(), WithinAbs(M::kAlongZMm, kPlaceMm));
}

TEST_CASE("AssemblyReference_SuppressedComponentsAreAbsentFromTheExportedFile",
          "[reference][assembly][p13][io][step]") {
    // Every STEP check above exports a model in its committed configuration.
    // The negative is the one that matters: a component suppressed in
    // another configuration must be ABSENT from the file, proved by reading
    // it back rather than by trusting the exporter's summary.
    TempDir dir;
    auto model = reference::buildMachineReferenceModel();
    REQUIRE(model.has_value());
    const ConfigurationId bare = model->bare;
    Document document = std::move(model->document);
    REQUIRE(document.setActiveConfiguration(bare).has_value());

    const auto path = dir.path() / "bare.step";
    const auto summary = io::exportStep(document, path);
    INFO((summary ? std::string{} : summary.error().message));
    REQUIRE(summary.has_value());
    // Three components left: the housing and its two shafts. Two products,
    // because both shafts are instances of one part.
    CHECK(summary->components.size() == 3);
    CHECK(summary->bodies.size() == 2);

    const auto structure = readStepStructure(path);
    REQUIRE(structure.has_value());
    REQUIRE(structure->instances.size() == 3);
    CHECK(structure->products.size() == 2);
    for (const test::StepShape& instance : structure->instances) {
        INFO("instance " << instance.name);
        CHECK(instance.name != "CoverPlate");
        CHECK_FALSE(instance.name.starts_with("Foot"));
    }
    // Measured, not merely counted: the material in the file is the housing
    // and two shafts, with the cover's 192000 mm^3 and the feet's 23040 gone.
    const double housing = 160.0 * 120.0 * 40.0;
    const double shafts = 2.0 * kPi * 12.0 * 12.0 * 140.0;
    const auto whole = readStepFile(path);
    REQUIRE(whole.has_value());
    CHECK_THAT(whole->volumeMm3, WithinRel(housing + shafts, 1e-9));
}

TEST_CASE("AssemblyReference_SurvivesACommandEditAndItsUndo", "[reference][assembly][p13]") {
    // P13-CMD-001 is the one P13 capability a committed FILE cannot carry,
    // because a command history is not part of a document. So it is
    // exercised here instead: a real command, executed and undone against a
    // committed reference model through the production CommandHistory.
    auto loaded = io::loadDocument(referenceDirectory() / "assembly_configured_frame.bcad");
    REQUIRE(loaded.has_value());
    Document document = std::move(*loaded);
    const ComponentId brace = componentNamed(document, "Brace1");

    const auto braceSuppressed = [&](const Document& doc) {
        return assembly::isComponentSuppressed(doc, brace);
    };
    const auto activeCount = [&](const Document& doc) { return assembly::activeComponents(doc).size(); };

    REQUIRE_FALSE(braceSuppressed(document));
    REQUIRE(activeCount(document) == 4);

    // The committed model has `Full` active, and a suppression override
    // belongs to a configuration.
    const auto active = document.configurations().active();
    REQUIRE(active.has_value());

    CommandHistory history;
    REQUIRE(history
                .execute(document, std::make_unique<assembly::SuppressComponentCommand>(*active, brace, true))
                .has_value());
    CHECK(braceSuppressed(document));
    CHECK(activeCount(document) == 3);
    {
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        CHECK(built.transform(brace) == nullptr);
    }

    REQUIRE(history.undo(document).has_value());
    CHECK_FALSE(braceSuppressed(document));
    CHECK(activeCount(document) == 4);
    {
        // And the assembly solves back to exactly what the committed model
        // solves to: an undo that left the document nearly right would show
        // up as a moved brace.
        Assembled restored{document.clone()};
        Assembled committed{build(AssemblyReferenceModelKind::ConfiguredFrame)};
        REQUIRE(restored.regenerated());
        REQUIRE(committed.regenerated());
        const RigidTransform3D* a = restored.transform(brace);
        const RigidTransform3D* b = committed.transform(componentNamed(committed.document(), "Brace1"));
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(positionMm(*a)[i] == positionMm(*b)[i]);
        }
    }

    REQUIRE(history.redo(document).has_value());
    CHECK(braceSuppressed(document));
    CHECK(activeCount(document) == 3);
}
