#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using assembly::SolveStatus;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-PERSIST-001: what a .bcad file says about an assembly.
//
// The failure this file exists to catch is a file that LOADS WITHOUT ERROR
// into a different assembly than was saved. It is the worst of the failures
// this phase has been built around, because a file outlives the session that
// wrote it: an exception on load is a good day, whereas a document that
// opens, looks right, and has one mate pointing somewhere else is discovered
// weeks later with the original long gone.
//
// So these tests compare the ASSEMBLY, not the bytes. A byte-identical
// rewrite proves the writer is deterministic; it says nothing about whether
// the reader understood what it read. The strongest available statement is
// that the loaded document SOLVES TO THE SAME TRANSFORMS, and that is what
// the round-trip cases assert.

namespace {

constexpr double kTolerance = 1e-9;

struct Rig {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId base{};
    ComponentId arm{};
};

Rig makeRig() {
    Rig rig;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    rig.sketch = require(rig.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(rig.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    rig.part = require(rig.document.addObject(std::move(*extrude)));
    rig.base = require(assembly::createComponent(rig.document, "Base", {.part = rig.part}));
    rig.arm = require(assembly::createComponent(
        rig.document, "Arm", {.part = rig.part, .placement = {.translation = {0_mm, 0_mm, 50_mm}}}));
    return rig;
}

MateTarget plane(ComponentId c, PrincipalPlane which = PrincipalPlane::XY) {
    return planeTarget(c, PlaneReference{.plane = which});
}
MateTarget axis(ComponentId c, PrincipalAxis which = PrincipalAxis::Z) {
    return axisTarget(c, AxisReference{.axis = which});
}

/// Saves, loads, and gives back the loaded document. The original is left
/// alone so the two can be compared.
Document roundTrip(const Document& document, const std::filesystem::path& path) {
    auto saved = io::saveDocument(document, path);
    if (!saved) {
        FAIL(saved.error().message);
    }
    auto loaded = io::loadDocument(path);
    if (!loaded) {
        FAIL(loaded.error().message);
    }
    return std::move(*loaded);
}

/// Everything the document canonically says about its assembly, compared as
/// a whole rather than field by field -- the discipline P13-CMD-001's defect
/// argued for.
void checkAssemblyEqual(const Document& before, const Document& after) {
    REQUIRE(assembly::components(after) == assembly::components(before));
    REQUIRE(assembly::mates(after) == assembly::mates(before));
    for (const ComponentId id : assembly::components(before)) {
        INFO("component " << id.value());
        const assembly::Component* a = assembly::findComponent(before, id);
        const assembly::Component* b = assembly::findComponent(after, id);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(b->definition() == a->definition());
        CHECK(b->name() == a->name());
        CHECK(b->dependencies() == a->dependencies());
    }
    for (const MateId id : assembly::mates(before)) {
        INFO("mate " << id.value());
        const assembly::Mate* a = assembly::findMate(before, id);
        const assembly::Mate* b = assembly::findMate(after, id);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(b->definition() == a->definition());
        CHECK(b->name() == a->name());
        CHECK(b->dependencies() == a->dependencies());
    }
    CHECK(after.activeConfiguration() == before.activeConfiguration());
    CHECK(equivalent(after.configurations(), before.configurations()));
}

/// Regenerates a document and returns what the assembly solved to.
struct Solved {
    features::Regenerator regenerator{};
    assembly::AssemblyRegeneration report{};

    void run(Document& document) {
        assembly::registerHandlers(regenerator, nullptr, &report);
        (void)regenerator.regenerate(document);
    }
};

} // namespace

// --- A, B: components and placements ---------------------------------------------------------------

TEST_CASE("Persist_AMinimalAssemblyRoundTrips", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const Document after = roundTrip(rig.document, dir.path() / "minimal.bcad");
    checkAssemblyEqual(rig.document, after);
    CHECK(assembly::components(after).size() == 2);
}

TEST_CASE("Persist_TwoInstancesOfOnePartStayDistinct", "[assembly][persist][p13][io]") {
    // The whole reason a component is not a part: same part reference,
    // different identity and different placement.
    TempDir dir;
    Rig rig = makeRig();
    const Document after = roundTrip(rig.document, dir.path() / "two.bcad");

    const assembly::Component* base = assembly::findComponent(after, rig.base);
    const assembly::Component* arm = assembly::findComponent(after, rig.arm);
    REQUIRE(base != nullptr);
    REQUIRE(arm != nullptr);
    CHECK(base->definition().part == arm->definition().part);
    CHECK(rig.base != rig.arm);
    CHECK(base->definition().placement.translation[2] == 0_mm);
    CHECK(arm->definition().placement.translation[2] == 50_mm);
    // Ordering is ascending ID, and survives.
    CHECK(assembly::components(after) == std::vector<ComponentId>{rig.base, rig.arm});
}

TEST_CASE("Persist_APlacementKeepsItsExactValueAndItsParameter", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const ParameterId lift = require(rig.document.createParameter("lift", 37_mm, units::mm));
    REQUIRE(assembly::setComponentDefinition(
                rig.document, rig.arm,
                {.part = rig.part,
                 .placement = {.translation = {11_mm, -13_mm, 0_mm},
                               .translationParameters = {std::nullopt, std::nullopt, lift},
                               .rotation = {5_deg, -7_deg, 19_deg}}})
                .has_value());

    const Document after = roundTrip(rig.document, dir.path() / "placed.bcad");
    const ComponentPlacement& placement = assembly::findComponent(after, rig.arm)->definition().placement;
    CHECK(placement.translation[0] == 11_mm);
    CHECK(placement.translation[1] == -13_mm);
    CHECK(placement.rotation[2] == 19_deg);
    // The parameter drives it, and the binding survives -- not just the value.
    REQUIRE(placement.translationParameters[2].has_value());
    CHECK(*placement.translationParameters[2] == lift);
    CHECK_THAT(require(assembly::placementOf(after, rig.arm)).apply(Point3D{}).z.si(),
               WithinAbs(0.037, kTolerance));
}

// --- C, D: every mate kind ---------------------------------------------------------------------------

TEST_CASE("Persist_EverySevenBasicMateKindRoundTrips", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    std::vector<MateId> ids;
    ids.push_back(require(assembly::createMate(rig.document, "Ground",
                                               {.type = MateType::Fixed, .component = rig.base})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Touch",
        {.type = MateType::Coincident, .a = plane(rig.base), .b = plane(rig.arm)})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Bore", {.type = MateType::Concentric, .a = axis(rig.base), .b = axis(rig.arm)})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Along", {.type = MateType::Parallel, .a = plane(rig.base), .b = plane(rig.arm)})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Square",
        {.type = MateType::Perpendicular, .a = plane(rig.base), .b = plane(rig.arm)})));
    ids.push_back(require(assembly::createMate(rig.document, "Gap",
                                               {.type = MateType::Distance,
                                                .a = plane(rig.base),
                                                .b = plane(rig.arm),
                                                .distance = -25_mm})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Tilt",
        {.type = MateType::Angle, .a = plane(rig.base), .b = plane(rig.arm), .angle = 35_deg})));
    REQUIRE(ids.size() == 7);

    const Document after = roundTrip(rig.document, dir.path() / "basic.bcad");
    checkAssemblyEqual(rig.document, after);
    // Values keep their sign and their exact magnitude.
    CHECK(assembly::findMate(after, ids[5])->definition().distance == -25_mm);
    CHECK(assembly::findMate(after, ids[6])->definition().angle == 35_deg);
}

TEST_CASE("Persist_EveryFourMechanicalMateKindRoundTrips", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    std::vector<MateId> ids;
    ids.push_back(require(assembly::createMate(
        rig.document, "Hinge", {.type = MateType::Revolute, .a = axis(rig.base), .b = axis(rig.arm)})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Slide",
        {.type = MateType::Slider,
         .a = axis(rig.base),
         .b = axis(rig.arm),
         .a2 = axis(rig.base, PrincipalAxis::X),
         .b2 = axis(rig.arm, PrincipalAxis::X)})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Sleeve",
        {.type = MateType::Cylindrical, .a = axis(rig.base), .b = axis(rig.arm)})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Face", {.type = MateType::Planar, .a = plane(rig.base), .b = plane(rig.arm)})));
    REQUIRE(ids.size() == 4);

    const Document after = roundTrip(rig.document, dir.path() / "mechanical.bcad");
    checkAssemblyEqual(rig.document, after);

    // The slider's roll reference in particular: without it, what comes back
    // is a sleeve wearing a slider's name.
    const MateDefinition& slide = assembly::findMate(after, ids[1])->definition();
    CHECK(slide.type == MateType::Slider);
    REQUIRE(slide.a2.has_value());
    REQUIRE(slide.b2.has_value());
    CHECK(slide.a2->component == rig.base);
    CHECK(slide.b2->component == rig.arm);
    CHECK(slide.a2->axis->axis == PrincipalAxis::X);
    // And the kinds that share equations are still told apart by name.
    CHECK(assembly::findMate(after, ids[2])->definition().type == MateType::Cylindrical);
    CHECK(assembly::findMate(after, ids[3])->definition().type == MateType::Planar);
}

TEST_CASE("Persist_ASuppressedMateKeepsItsOwnFlag", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const MateId id = require(assembly::createMate(rig.document, "Touch",
                                                   {.type = MateType::Coincident,
                                                    .a = plane(rig.base),
                                                    .b = plane(rig.arm),
                                                    .suppressed = true}));
    const Document after = roundTrip(rig.document, dir.path() / "suppressed.bcad");
    CHECK(assembly::findMate(after, id)->definition().suppressed);
    CHECK(assembly::isMateSuppressed(after, id));
}

// --- E: configurations and suppression -------------------------------------------------------------------

TEST_CASE("Persist_ThreeConfigurationsAndTheirSuppressionRoundTrip", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const MateId touch = require(assembly::createMate(
        rig.document, "Touch",
        {.type = MateType::Coincident, .a = plane(rig.base), .b = plane(rig.arm)}));
    const ConfigurationId a = require(rig.document.createConfiguration("A"));
    const ConfigurationId b = require(rig.document.createConfiguration("B"));
    const ConfigurationId c = require(rig.document.createConfiguration("C"));
    REQUIRE(assembly::suppressComponent(rig.document, a, rig.arm, true).has_value());
    REQUIRE(assembly::suppressMate(rig.document, b, touch, true).has_value());
    REQUIRE(assembly::suppressComponent(rig.document, c, rig.arm, false).has_value());
    REQUIRE(rig.document.setActiveConfiguration(b).has_value());

    const Document after = roundTrip(rig.document, dir.path() / "configs.bcad");
    checkAssemblyEqual(rig.document, after);
    CHECK(after.configurations().size() == 3);
    CHECK(after.activeConfiguration() == b);

    // Each configuration's own opinion, including the explicit false and the
    // absence of an opinion -- three states, not two.
    CHECK(after.configurations().find(a)->suppressionFor(rig.arm) == std::optional<bool>{true});
    CHECK(after.configurations().find(b)->suppressionFor(touch) == std::optional<bool>{true});
    CHECK(after.configurations().find(c)->suppressionFor(rig.arm) == std::optional<bool>{false});
    CHECK_FALSE(after.configurations().find(a)->suppressionFor(touch).has_value());
}

TEST_CASE("Persist_SwitchingThroughEveryConfigurationAfterLoadGivesTheSameActiveSets",
          "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const MateId touch = require(assembly::createMate(
        rig.document, "Touch",
        {.type = MateType::Coincident, .a = plane(rig.base), .b = plane(rig.arm)}));
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    const ConfigurationId loose = require(rig.document.createConfiguration("Loose"));
    REQUIRE(assembly::suppressComponent(rig.document, lean, rig.arm, true).has_value());
    REQUIRE(assembly::suppressMate(rig.document, loose, touch, true).has_value());

    Document after = roundTrip(rig.document, dir.path() / "switch.bcad");
    for (const std::optional<ConfigurationId> id :
         {std::optional<ConfigurationId>{}, std::optional<ConfigurationId>{lean},
          std::optional<ConfigurationId>{loose}}) {
        REQUIRE(rig.document.setActiveConfiguration(id).has_value());
        REQUIRE(after.setActiveConfiguration(id).has_value());
        INFO("configuration " << (id ? std::to_string(id->value()) : std::string{"base"}));
        CHECK(assembly::activeComponents(after) == assembly::activeComponents(rig.document));
        CHECK(assembly::activeMates(after) == assembly::activeMates(rig.document));
    }
}

// --- F: stable and unresolved references -------------------------------------------------------------------

TEST_CASE("Persist_AFaceTargetKeepsItsFeatureAndRole", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const MateId id = require(assembly::createMate(
        rig.document, "OnCap",
        {.type = MateType::Coincident,
         .a = faceTarget(rig.base, FaceName{.feature = rig.part, .face = {.role = FaceRole::EndCap}}),
         .b = plane(rig.arm)}));

    const Document after = roundTrip(rig.document, dir.path() / "face.bcad");
    const MateDefinition& d = assembly::findMate(after, id)->definition();
    REQUIRE(d.a->face.has_value());
    CHECK(d.a->face->feature == rig.part);
    CHECK(d.a->face->face.role == FaceRole::EndCap);
    CHECK(d.a->component == rig.base);
}

TEST_CASE("Persist_AnExternalReferenceKeepsItsDocumentIdentityAndLocator",
          "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const DocumentId owner = DocumentId::fromValue(Uuid::generateV4());
    const ObjectReference external{owner, rig.part, "parts/block.bcad"};
    const ComponentId id =
        require(assembly::createComponent(rig.document, "Imported", {.part = external}));

    const Document after = roundTrip(rig.document, dir.path() / "external.bcad");
    const ObjectReference& back = assembly::findComponent(after, id)->definition().part;
    CHECK(back == external);
    CHECK(sameTarget(back, external));
    REQUIRE(back.document.has_value());
    CHECK(*back.document == owner);
    CHECK(back.hint == "parts/block.bcad");
    // And it is still external, so it names nothing local.
    CHECK_FALSE(isInternal(back));
}

TEST_CASE("Persist_AnUnresolvedReferenceStaysUnresolvedAndThenRecovers",
          "[assembly][persist][p13][io]") {
    // Saved broken, loaded broken, and repaired afterwards -- with the
    // canonical identity unchanged throughout.
    TempDir dir;
    Rig rig = makeRig();
    const MateId id = require(assembly::createMate(
        rig.document, "OnCap",
        {.type = MateType::Coincident,
         .a = faceTarget(rig.base, FaceName{.feature = rig.part, .face = {.role = FaceRole::EndCap}}),
         .b = plane(rig.arm)}));
    const MateDefinition before = assembly::findMate(rig.document, id)->definition();
    auto removed = rig.document.removeObject(rig.part);
    REQUIRE(removed.has_value());

    Document after = roundTrip(rig.document, dir.path() / "broken.bcad");
    // The reference is intact and still names what is missing.
    CHECK(assembly::findMate(after, id)->definition() == before);
    features::Regenerator regenerator;
    assembly::registerHandlers(regenerator);
    (void)regenerator.regenerate(after);
    const features::BodyLookup bodies = [&](ObjectId o) { return regenerator.body(o); };
    CHECK_FALSE(assembly::unresolvedMateTargets(after, bodies).empty());

    // Bring the part back under its own ID.
    REQUIRE(after.insertObject(std::move(*removed)).has_value());
    requireReport(regenerator, after);
    const features::BodyLookup repaired = [&](ObjectId o) { return regenerator.body(o); };
    CHECK(assembly::unresolvedMateTargets(after, repaired).empty());
    CHECK(assembly::findMate(after, id)->definition() == before);
}

// --- Derived state, and strong IDs ---------------------------------------------------------------------------

TEST_CASE("Persist_NoDerivedStateReachesTheFile", "[assembly][persist][p13][io]") {
    // The hard gate. A solve produces transforms; the file must be identical
    // whether or not one has run.
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Ground",
                                 {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Touch",
                                 {.type = MateType::Coincident,
                                  .a = plane(rig.base),
                                  .b = plane(rig.arm)})
                .has_value());

    const auto beforeSolve = dir.path() / "before.bcad";
    REQUIRE(io::saveDocument(rig.document, beforeSolve).has_value());

    Solved solved;
    solved.run(rig.document);
    REQUIRE(solved.report.transforms == 2);
    REQUIRE(solved.regenerator.transform(rig.arm) != nullptr);

    const auto afterSolve = dir.path() / "after.bcad";
    REQUIRE(io::saveDocument(rig.document, afterSolve).has_value());
    CHECK(readFile(afterSolve) == readFile(beforeSolve));

    // And nothing in the text names a solver concept.
    const std::string text = readFile(afterSolve);
    for (const std::string_view forbidden :
         {"transform", "residual", "jacobian", "iterations", "dof", "solved"}) {
        INFO("looking for " << forbidden);
        CHECK_THAT(text, !ContainsSubstring(std::string{forbidden}));
    }
}

TEST_CASE("Persist_DerivedStateIsFullyRecoverableAfterBeingDiscarded",
          "[assembly][persist][p13][io]") {
    // Nothing derived is saved, so everything derived must be rebuildable
    // from what is. Throw the regenerator away entirely and start again.
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Ground",
                                 {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Touch",
                                 {.type = MateType::Coincident,
                                  .a = plane(rig.base),
                                  .b = plane(rig.arm)})
                .has_value());
    Solved first;
    first.run(rig.document);
    const RigidTransform3D original = *first.regenerator.transform(rig.arm);

    Document after = roundTrip(rig.document, dir.path() / "derived.bcad");
    Solved second; // a brand new regenerator, with nothing carried over
    second.run(after);
    REQUIRE(second.regenerator.transform(rig.arm) != nullptr);
    CHECK(*second.regenerator.transform(rig.arm) == original);
}

TEST_CASE("Persist_StrongIdsSurviveExactly", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const MateId mate = require(assembly::createMate(
        rig.document, "Touch",
        {.type = MateType::Coincident, .a = plane(rig.base), .b = plane(rig.arm)}));
    const ConfigurationId configuration = require(rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig.document, configuration, rig.arm, true).has_value());
    const DocumentId documentId = rig.document.id();

    const Document after = roundTrip(rig.document, dir.path() / "ids.bcad");
    CHECK(after.id() == documentId);
    CHECK(assembly::findComponent(after, rig.base) != nullptr);
    CHECK(assembly::findComponent(after, rig.arm) != nullptr);
    CHECK(assembly::findMate(after, mate) != nullptr);
    CHECK(after.configurations().find(configuration) != nullptr);
    // The mate still names the same components by ID, not by position.
    CHECK(assembly::findMate(after, mate)->definition().a->component == rig.base);
    CHECK(assembly::findMate(after, mate)->definition().b->component == rig.arm);
}

// --- J: legacy --------------------------------------------------------------------------------------------

TEST_CASE("Persist_APreAssemblyFileStillLoadsAndHasNoAssembly", "[assembly][persist][p13][io]") {
    // examples/models/plate.bcad was written for the P0-P10 foundation and
    // has not been touched since. It predates parameters' expressions,
    // configurations, and every part of P13. It must still load, and it must
    // describe an assembly of nothing rather than failing to mention one.
    const std::filesystem::path legacy =
        std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "plate.bcad";
    REQUIRE(std::filesystem::exists(legacy));

    auto loaded = io::loadDocument(legacy);
    REQUIRE(loaded.has_value());
    CHECK(assembly::components(*loaded).empty());
    CHECK(assembly::mates(*loaded).empty());
    CHECK(loaded->configurations().empty());
    CHECK_FALSE(loaded->activeConfiguration().has_value());

    // It regenerates, and solving an assembly of nothing is not an error.
    Solved solved;
    solved.run(*loaded);
    CHECK(solved.report.transforms == 0);
    auto result = assembly::solve(*loaded);
    REQUIRE(result.has_value());
    CHECK(result->unknowns == 0);
}

TEST_CASE("Persist_EveryCommittedLegacyModelStillLoads", "[assembly][persist][p13][io]") {
    // The whole corpus, not one file: 24 models written across P0-P12, none
    // of which contains an assembly. Any that stopped loading would be a
    // compatibility regression.
    const std::filesystem::path models{BETTERCAD_EXAMPLE_MODELS_DIR};
    REQUIRE(std::filesystem::exists(models));
    std::size_t checked = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(models)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".bcad") {
            continue;
        }
        INFO("file " << entry.path().filename().string());
        auto loaded = io::loadDocument(entry.path());
        REQUIRE(loaded.has_value());
        // Pre-assembly documents, all of them.
        CHECK(assembly::components(*loaded).empty());
        ++checked;
    }
    CHECK(checked >= 20);
}

// --- K: corrupt input -------------------------------------------------------------------------------------

TEST_CASE("Persist_RefusesPlausiblyCorruptAssemblyData", "[assembly][persist][p13][io]") {
    // Not obviously mangled files -- those are easy. These are the edits that
    // produce a file which parses as JSON and describes an assembly that
    // cannot be right.
    TempDir dir;
    Rig rig = makeRig();
    const MateId touch = require(assembly::createMate(
        rig.document, "Touch",
        {.type = MateType::Coincident, .a = plane(rig.base), .b = plane(rig.arm)}));
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig.document, lean, rig.arm, true).has_value());
    const auto good = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(rig.document, good).has_value());
    const std::string original = readFile(good);

    const auto refuses = [&](std::string_view what, const std::string& text) {
        const auto path = dir.path() / "corrupt.bcad";
        writeFile(path, text);
        auto loaded = io::loadDocument(path);
        INFO("case: " << what);
        CHECK_FALSE(loaded.has_value());
        return loaded.has_value();
    };
    const auto replaced = [&](std::string_view from, std::string_view to) {
        std::string text = original;
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        return text;
    };

    refuses("an unknown mate kind", replaced("\"coincident\"", "\"teleport\""));
    refuses("an unknown object type", replaced("\"component\"", "\"wormhole\""));
    refuses("a suppression override for an object never written",
            replaced(std::format("\"object\": {}", rig.arm.value()), "\"object\": 987654"));
    refuses("an unsupported format version", replaced("\"version\": 1", "\"version\": 99"));
    refuses("a wrong JSON type for a number", replaced("\"version\": 1", "\"version\": \"one\""));
    refuses("a truncated file", original.substr(0, original.size() / 2));
    refuses("an empty file", std::string{});
    refuses("not JSON at all", std::string{"this is not a document"});

    // The good file still loads afterwards: nothing above left state behind.
    auto still = io::loadDocument(good);
    CHECK(still.has_value());
    CHECK(assembly::findMate(*still, touch) != nullptr);
}

TEST_CASE("Persist_RefusesNonFiniteNumbers", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Gap",
                                 {.type = MateType::Distance,
                                  .a = plane(rig.base),
                                  .b = plane(rig.arm),
                                  .distance = 25_mm})
                .has_value());
    const auto good = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(rig.document, good).has_value());

    for (const std::string_view bad : {"NaN", "Infinity", "-Infinity", "1e999"}) {
        std::string text = readFile(good);
        const std::size_t at = text.find("\"distance\": 0.025");
        REQUIRE(at != std::string::npos);
        text.replace(at, std::string_view{"\"distance\": 0.025"}.size(),
                     std::format("\"distance\": {}", bad));
        const auto path = dir.path() / "nan.bcad";
        writeFile(path, text);
        INFO("value: " << bad);
        CHECK_FALSE(io::loadDocument(path).has_value());
    }
}

// --- Failure atomicity ------------------------------------------------------------------------------------

TEST_CASE("Persist_AFailedLoadLeavesTheCallersDocumentAlone", "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const std::size_t objects = rig.document.objectCount();
    const auto revision = rig.document.revision();

    const auto path = dir.path() / "rubbish.bcad";
    writeFile(path, "{ not a document");
    auto loaded = io::loadDocument(path);
    CHECK_FALSE(loaded.has_value());
    // loadDocument builds a new document and returns it; a failure returns
    // nothing at all, so there is no half-built one to leak.
    CHECK(rig.document.objectCount() == objects);
    CHECK(rig.document.revision() == revision);

    // And a valid load still works afterwards.
    const auto good = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(rig.document, good).has_value());
    CHECK(io::loadDocument(good).has_value());
}

TEST_CASE("Persist_AFailedSaveLeavesNoPartialFile", "[assembly][persist][p13][io]") {
    // Saving is atomic: a temporary is written and renamed, so a path that
    // cannot be written leaves nothing behind.
    TempDir dir;
    Rig rig = makeRig();
    const auto impossible = dir.path() / "no-such-directory" / "out.bcad";
    auto saved = io::saveDocument(rig.document, impossible);
    CHECK_FALSE(saved.has_value());
    CHECK_FALSE(std::filesystem::exists(impossible));
    CHECK_FALSE(std::filesystem::exists(impossible.string() + ".tmp"));

    // The document is unharmed and saves fine elsewhere.
    const auto fine = dir.path() / "fine.bcad";
    CHECK(io::saveDocument(rig.document, fine).has_value());
    CHECK(io::loadDocument(fine).has_value());
}

// --- Deterministic serialization ----------------------------------------------------------------------------

TEST_CASE("Persist_SerializationIsByteIdenticalAndStable", "[assembly][persist][p13][io][determinism]") {
    // Three statements: saving twice gives the same bytes; saving what was
    // loaded gives the same bytes; and a second round trip changes nothing.
    // The last is the property-style one -- serialize(deserialize(serialize(x)))
    // == serialize(x) -- which catches a reader that quietly normalises.
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Slide",
                                 {.type = MateType::Slider,
                                  .a = axis(rig.base),
                                  .b = axis(rig.arm),
                                  .a2 = axis(rig.base, PrincipalAxis::X),
                                  .b2 = axis(rig.arm, PrincipalAxis::X)})
                .has_value());
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig.document, lean, rig.arm, true).has_value());
    REQUIRE(rig.document.setActiveConfiguration(lean).has_value());

    const auto first = dir.path() / "first.bcad";
    const auto second = dir.path() / "second.bcad";
    REQUIRE(io::saveDocument(rig.document, first).has_value());
    REQUIRE(io::saveDocument(rig.document, second).has_value());
    CHECK(readFile(second) == readFile(first));

    auto loaded = io::loadDocument(first);
    REQUIRE(loaded.has_value());
    const auto third = dir.path() / "third.bcad";
    REQUIRE(io::saveDocument(*loaded, third).has_value());
    CHECK(readFile(third) == readFile(first));

    auto again = io::loadDocument(third);
    REQUIRE(again.has_value());
    const auto fourth = dir.path() / "fourth.bcad";
    REQUIRE(io::saveDocument(*again, fourth).has_value());
    CHECK(readFile(fourth) == readFile(first));
}

TEST_CASE("Persist_OrderDoesNotDependOnInsertionOrder", "[assembly][persist][p13][io][determinism]") {
    // Two documents with the same content built in different orders will have
    // different IDs, so their bytes differ -- but each must serialize its own
    // content in ascending ID order rather than in insertion order.
    TempDir dir;
    Rig rig = makeRig();
    const MateId later = require(assembly::createMate(
        rig.document, "Zebra", {.type = MateType::Fixed, .component = rig.base}));
    const MateId earlier = require(assembly::createMate(
        rig.document, "Alpha", {.type = MateType::Fixed, .component = rig.arm}));
    REQUIRE(later.value() < earlier.value());

    const auto path = dir.path() / "ordered.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    const std::string text = readFile(path);
    // "Zebra" has the lower ID, so it is written first despite its name.
    CHECK(text.find("\"Zebra\"") < text.find("\"Alpha\""));
    // And the loaded order is the same ascending-ID order.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(assembly::mates(*loaded) == std::vector<MateId>{later, earlier});
}

// --- G, H, I, L: the hard gate, save -> load -> regenerate -> solve -------------------------------------------

namespace {

/// Builds an assembly, regenerates and solves it, saves it, loads it,
/// regenerates and solves that, and compares everything both say.
void checkRoundTripSolvesTheSame(Rig& rig, const std::filesystem::path& path,
                                 SolveStatus expectedStatus, std::size_t expectedDof) {
    Solved before;
    before.run(rig.document);
    auto originalSolve = assembly::solve(rig.document);
    REQUIRE(originalSolve.has_value());
    CHECK(originalSolve->status == expectedStatus);
    CHECK(originalSolve->degreesOfFreedom == expectedDof);

    Document after = roundTrip(rig.document, path);
    checkAssemblyEqual(rig.document, after);

    Solved reloaded;
    reloaded.run(after);
    auto reloadedSolve = assembly::solve(after);
    REQUIRE(reloadedSolve.has_value());

    CHECK(reloadedSolve->status == originalSolve->status);
    CHECK(reloadedSolve->degreesOfFreedom == originalSolve->degreesOfFreedom);
    CHECK(reloadedSolve->unknowns == originalSolve->unknowns);
    CHECK(reloadedSolve->equations == originalSolve->equations);
    CHECK(reloadedSolve->maxResidual == originalSolve->maxResidual);
    CHECK(reloadedSolve->conflicting == originalSolve->conflicting);
    CHECK(reloadedSolve->redundant == originalSolve->redundant);
    REQUIRE(reloadedSolve->transforms.size() == originalSolve->transforms.size());
    for (const auto& [id, transform] : originalSolve->transforms) {
        INFO("component " << id.value());
        REQUIRE(reloadedSolve->transforms.contains(id));
        // Bit-identical: the solve starts from intent alone and nothing is
        // seeded, so a file that preserved the intent exactly must reproduce
        // the arithmetic exactly.
        CHECK(reloadedSolve->transforms.at(id) == transform);
    }
    // And through regeneration, which is the path a real caller takes.
    CHECK(reloaded.report.transforms == before.report.transforms);
    for (const auto& [id, transform] : before.regenerator.transforms()) {
        REQUIRE(reloaded.regenerator.transform(id) != nullptr);
        CHECK(*reloaded.regenerator.transform(id) == transform);
    }
}

} // namespace

TEST_CASE("Persist_AFullyConstrainedAssemblySolvesTheSameAfterALoad",
          "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Ground",
                                 {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Touch",
                                 {.type = MateType::Coincident,
                                  .a = plane(rig.base),
                                  .b = plane(rig.arm)})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "AcrossX",
                                 {.type = MateType::Distance,
                                  .a = plane(rig.base, PrincipalPlane::YZ),
                                  .b = plane(rig.arm, PrincipalPlane::YZ),
                                  .distance = 0_mm})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "AcrossY",
                                 {.type = MateType::Distance,
                                  .a = plane(rig.base, PrincipalPlane::XZ),
                                  .b = plane(rig.arm, PrincipalPlane::XZ),
                                  .distance = 0_mm})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Square",
                                 {.type = MateType::Perpendicular,
                                  .a = plane(rig.base, PrincipalPlane::YZ),
                                  .b = plane(rig.arm, PrincipalPlane::XZ)})
                .has_value());
    checkRoundTripSolvesTheSame(rig, dir.path() / "tight.bcad", SolveStatus::FullyConstrained, 0);
}

TEST_CASE("Persist_AnUnderConstrainedAssemblySolvesTheSameAfterALoad",
          "[assembly][persist][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Ground",
                                 {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Sleeve",
                                 {.type = MateType::Cylindrical,
                                  .a = axis(rig.base),
                                  .b = axis(rig.arm)})
                .has_value());
    checkRoundTripSolvesTheSame(rig, dir.path() / "loose.bcad", SolveStatus::UnderConstrained, 2);
}

TEST_CASE("Persist_AnInconsistentAssemblyIsStillInconsistentAfterALoad",
          "[assembly][persist][p13][io]") {
    // A file must preserve an assembly that does not work as faithfully as
    // one that does -- including the diagnosis.
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Ground",
                                 {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Near",
                                 {.type = MateType::Distance,
                                  .a = plane(rig.base),
                                  .b = plane(rig.arm),
                                  .distance = 10_mm})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Far",
                                 {.type = MateType::Distance,
                                  .a = plane(rig.base),
                                  .b = plane(rig.arm),
                                  .distance = 20_mm})
                .has_value());

    auto original = assembly::solve(rig.document);
    REQUIRE(original.has_value());
    REQUIRE(original->status == SolveStatus::Inconsistent);

    const Document after = roundTrip(rig.document, dir.path() / "broken.bcad");
    auto reloaded = assembly::solve(after);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->status == SolveStatus::Inconsistent);
    CHECK(reloaded->conflicting == original->conflicting);
    // The least-squares compromise is identical, not merely also wrong.
    CHECK(reloaded->maxResidual == original->maxResidual);
}

TEST_CASE("Persist_ALargeMixedAssemblyRoundTripsAndSolvesTheSame", "[assembly][persist][p13][io]") {
    // Everything at once: many components, basic and mechanical mates,
    // configurations, suppression, a parameter-driven placement and a face
    // reference.
    TempDir dir;
    Rig rig = makeRig();
    const ParameterId lift = require(rig.document.createParameter("lift", 45_mm, units::mm));
    std::vector<ComponentId> extra;
    for (int i = 0; i < 6; ++i) {
        extra.push_back(require(assembly::createComponent(
            rig.document, std::format("Block{}", i),
            {.part = rig.part,
             .placement = {.translation = {Length::fromSi(0.01 * (i + 1)), 0_mm, 20_mm}}})));
    }
    REQUIRE(assembly::setComponentDefinition(
                rig.document, rig.arm,
                {.part = rig.part,
                 .placement = {.translation = {0_mm, 0_mm, 0_mm},
                               .translationParameters = {std::nullopt, std::nullopt, lift}}})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Ground",
                                 {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Hinge",
                                 {.type = MateType::Revolute,
                                  .a = axis(rig.base),
                                  .b = axis(extra[0])})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Slide",
                                 {.type = MateType::Slider,
                                  .a = axis(rig.base),
                                  .b = axis(extra[1]),
                                  .a2 = axis(rig.base, PrincipalAxis::X),
                                  .b2 = axis(extra[1], PrincipalAxis::X)})
                .has_value());
    const MateId onCap = require(assembly::createMate(
        rig.document, "OnCap",
        {.type = MateType::Coincident,
         .a = faceTarget(rig.base, FaceName{.feature = rig.part, .face = {.role = FaceRole::EndCap}}),
         .b = plane(extra[2])}));
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig.document, lean, extra[3], true).has_value());
    REQUIRE(assembly::suppressMate(rig.document, lean, onCap, true).has_value());
    REQUIRE(rig.document.setActiveConfiguration(lean).has_value());

    Solved before;
    before.run(rig.document);
    REQUIRE(before.report.status.has_value());

    Document after = roundTrip(rig.document, dir.path() / "large.bcad");
    checkAssemblyEqual(rig.document, after);

    Solved reloaded;
    reloaded.run(after);
    REQUIRE(reloaded.report.status.has_value());
    CHECK(*reloaded.report.status == *before.report.status);
    CHECK(reloaded.report.degreesOfFreedom == before.report.degreesOfFreedom);
    CHECK(reloaded.report.transforms == before.report.transforms);
    for (const auto& [id, transform] : before.regenerator.transforms()) {
        INFO("component " << id.value());
        REQUIRE(reloaded.regenerator.transform(id) != nullptr);
        CHECK(*reloaded.regenerator.transform(id) == transform);
    }
}

TEST_CASE("Persist_AMateNamingAMissingComponentLoadsAndIsReported", "[assembly][persist][p13][io]") {
    // NOT refused, and deliberately so. P13-REF-001 chose "report, don't
    // refuse" for a reference that cannot be satisfied, and P13-STREF-001
    // requires a broken reference to persist as broken -- "not dropped on
    // save, not repaired on load" -- so a document must open in order to be
    // repaired. Refusing it would make a fixable file unopenable.
    //
    // What must not happen is that it loads SILENTLY. The contract is that
    // the document says what is wrong with it, which is what this asserts.
    TempDir dir;
    Rig rig = makeRig();
    const MateId touch = require(assembly::createMate(
        rig.document, "Touch",
        {.type = MateType::Coincident, .a = plane(rig.base), .b = plane(rig.arm)}));
    const auto good = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(rig.document, good).has_value());

    std::string text = readFile(good);
    const std::string needle = std::format("\"component\": {}", rig.arm.value());
    const std::size_t at = text.rfind(needle); // the mate's target, not the component itself
    REQUIRE(at != std::string::npos);
    text.replace(at, needle.size(), "\"component\": 987654");
    const auto edited = dir.path() / "dangling.bcad";
    writeFile(edited, text);

    auto loaded = io::loadDocument(edited);
    REQUIRE(loaded.has_value());

    // The mate is there and still says what it meant.
    const assembly::Mate* mate = assembly::findMate(*loaded, touch);
    REQUIRE(mate != nullptr);
    CHECK(mate->definition().b->component == ComponentId::fromValue(987654));

    // And the document reports the dangling reference rather than hiding it.
    const DocumentGraph graph = buildDependencyGraph(*loaded);
    const bool reported = std::ranges::any_of(graph.missing, [&](const MissingReference& missing) {
        return missing.dependent == ObjectId{touch} && missing.missing == ObjectId::fromValue(987654);
    });
    CHECK(reported);

    // Regeneration fails the mate rather than solving an assembly that is
    // missing one of the parts it constrains.
    features::Regenerator regenerator;
    assembly::AssemblyRegeneration report;
    assembly::registerHandlers(regenerator, nullptr, &report);
    auto pass = regenerator.regenerate(*loaded);
    REQUIRE(pass.has_value());
    CHECK_FALSE(pass->succeeded());
    CHECK(report.transforms == 0);
}
