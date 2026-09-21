#include "cli/CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Commands.hpp>
#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Naming.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::addRectangle;
using bettercad::test::cliPath;
using bettercad::test::readFile;
using bettercad::test::require;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-CLI-001: an assembly built, edited and solved without a window.
//
// Two things this file is watching for, because they are the failures the
// milestone exists to prevent.
//
// THE SCRIPT THAT HALF SUCCEEDS. Step seven of twenty fails and the file on
// disk holds the first six edits -- neither the document the script describes
// nor the one it started from, with nothing saying so. So every atomicity
// case here compares the FILE'S BYTES before and after, not the exit code: a
// command can report failure and still have written something.
//
// A NAME BECOMING IDENTITY. ADR-003 and the qualified
// Reference_IdentityDoesNotFollowNames say a locator is never identity. A CLI
// that accepted names could quietly undo that, so the disjointness the
// selector grammar rests on is pinned directly rather than assumed.
//
// Every case checks the exit code the process would return: runCliCommand()
// calls cli::run(), which is exactly what main() casts to its return value.
// The scripted workflows are also run through the real executable, by the
// cli.assembly.* process tests in tests/CMakeLists.txt.

namespace {

/// A document holding one part: a 40 x 30 x 10 mm block named "Block", and a
/// length parameter "lift" for the placement-binding cases.
struct PartDocument {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
    ParameterId lift{};
};

PartDocument makePart() {
    PartDocument p;
    p.lift = require(p.document.createParameter("lift", 50_mm, units::mm));
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    p.sketch = require(p.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(p.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    p.part = require(p.document.addObject(std::move(*extrude)));
    return p;
}

std::filesystem::path save(const TempDir& dir, const Document& document, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(document, path).has_value());
    return path;
}

/// A saved part document, ready to be edited through the CLI.
std::filesystem::path savedPart(const TempDir& dir, const std::string& name = "assembly.bcad") {
    return save(dir, makePart().document, name);
}

Document load(const std::filesystem::path& path) {
    auto document = io::loadDocument(path);
    REQUIRE(document.has_value());
    return std::move(*document);
}

void writeScript(const std::filesystem::path& path, const std::vector<std::string>& lines) {
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.good());
    for (const std::string& line : lines) {
        file << line << '\n';
    }
}

/// Two components, "Base" grounded and "Arm" 50 mm up, ready to be mated.
std::filesystem::path twoComponents(const TempDir& dir, const std::string& name = "assembly.bcad") {
    const auto path = savedPart(dir, name);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"component-add", file, "--part", "Block", "--name", "Base"}).exitCode ==
            ExitCode::Success);
    REQUIRE(runCliCommand({"component-add", file, "--part", "Block", "--name", "Arm", "--z", "50mm"}).exitCode ==
            ExitCode::Success);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "fixed", "--component", "Base", "--name", "Ground"})
                .exitCode == ExitCode::Success);
    return path;
}

ComponentId componentNamed(const Document& document, std::string_view name) {
    const DocumentObject* object = document.findObjectByName(name);
    REQUIRE(object != nullptr);
    return ComponentId::fromValue(object->id().value());
}

MateId mateNamed(const Document& document, std::string_view name) {
    const DocumentObject* object = document.findObjectByName(name);
    REQUIRE(object != nullptr);
    return MateId::fromValue(object->id().value());
}

/// Solves @p document the way the CLI does: regenerate with the assembly
/// handlers registered, then solve against the bodies that produced.
assembly::AssemblySolveResult solveLikeTheCli(Document& document) {
    features::Regenerator regenerator;
    assembly::registerHandlers(regenerator);
    REQUIRE(regenerator.regenerateAll(document).has_value());
    auto result = assembly::solve(document, {}, [&regenerator](ObjectId id) { return regenerator.body(id); });
    REQUIRE(result.has_value());
    return *result;
}

} // namespace

// --- The selector contract (ADR-009) ---------------------------------------

TEST_CASE("AssemblyCli_NamesAndIdsCannotOverlapByConstruction", "[cli][assembly][p13]") {
    // The whole selector grammar rests on this: a decimal selector can only
    // ever be an ID, because no NAME can be decimal. That is a property of
    // validateIdentifier(), not a convention the CLI parser maintains, so it
    // is pinned here. If anyone relaxes the identifier rule to admit a
    // leading digit, this fails before a file exists that depends on it.
    for (const std::string_view decimal : {"0", "1", "7", "42", "999", "18446744073709551615"}) {
        INFO("decimal selector " << decimal);
        CHECK_FALSE(validateIdentifier(decimal, "object").has_value());
    }
    // And a name always starts with a letter or an underscore, so it can
    // never be read as an ID.
    for (const std::string_view name : {"Base", "_hidden", "Arm2", "x"}) {
        INFO("name " << name);
        CHECK(validateIdentifier(name, "object").has_value());
        CHECK(std::isdigit(static_cast<unsigned char>(name.front())) == 0);
    }
}

TEST_CASE("AssemblyCli_SelectorAcceptsEitherAnIdOrAName", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    const ComponentId arm = componentNamed(load(path), "Arm");

    // By name.
    REQUIRE(runCliCommand({"component-place", file, "Arm", "--x", "10mm"}).exitCode == ExitCode::Success);
    // By the ID that names the same component.
    REQUIRE(runCliCommand({"component-place", file, std::to_string(arm.value()), "--y", "20mm"}).exitCode ==
            ExitCode::Success);

    const Document after = load(path);
    const assembly::Component* component = assembly::findComponent(after, arm);
    REQUIRE(component != nullptr);
    CHECK(component->definition().placement.translation[0].in(units::mm) == 10.0);
    CHECK(component->definition().placement.translation[1].in(units::mm) == 20.0);
    // The axis neither command named is untouched.
    CHECK(component->definition().placement.translation[2].in(units::mm) == 50.0);
}

TEST_CASE("AssemblyCli_SelectorRejectsWhatIsNeitherIdNorName", "[cli][assembly][p13]") {
    TempDir dir;
    const std::string file = cliPath(twoComponents(dir));
    const auto run = runCliCommand({"component-place", file, "7abc", "--x", "1mm"});
    // The CLI's own parser could not read it, so this is a usage error.
    CHECK(run.exitCode == ExitCode::UsageError);
    CHECK_THAT(run.err, ContainsSubstring("neither an ID (digits) nor a name"));
}

TEST_CASE("AssemblyCli_IdentityIsTheIdAndDoesNotFollowARename", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    const ComponentId arm = componentNamed(load(path), "Arm");

    // Rename it through the core API, as any other client might.
    {
        Document document = load(path);
        REQUIRE(document.rename(ObjectId{arm}, "Boom").has_value());
        REQUIRE(io::saveDocument(document, path).has_value());
    }

    // The old name names nothing: a name is a lookup, never identity.
    const auto byOldName = runCliCommand({"component-place", file, "Arm", "--x", "5mm"});
    CHECK(byOldName.exitCode == ExitCode::Failure);
    CHECK_THAT(byOldName.err, ContainsSubstring("nothing named 'Arm'"));

    // The ID still names the same component, which is what identity means.
    CHECK(runCliCommand({"component-place", file, std::to_string(arm.value()), "--x", "5mm"}).exitCode ==
          ExitCode::Success);
    CHECK(assembly::findComponent(load(path), arm)->definition().placement.translation[0].in(units::mm) == 5.0);
}

// --- Create, save and load --------------------------------------------------

TEST_CASE("AssemblyCli_ComponentAddSavesAComponentThatLoadsBack", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const Document before = load(path);
    const ObjectId part = before.findObjectByName("Block")->id();

    const auto run = runCliCommand({"component-add", cliPath(path), "--part", "Block", "--name", "Base"});
    REQUIRE(run.exitCode == ExitCode::Success);
    CHECK_THAT(run.out, ContainsSubstring("Created Base (object:"));
    CHECK_THAT(run.out, ContainsSubstring("placing Block"));

    const Document after = load(path);
    const std::vector<ComponentId> components = assembly::components(after);
    REQUIRE(components.size() == 1);
    const assembly::Component* component = assembly::findComponent(after, components.front());
    REQUIRE(component != nullptr);
    CHECK(component->name() == "Base");
    CHECK(component->definition().part.object == part);
    CHECK(isIdentity(component->definition().placement));
}

TEST_CASE("AssemblyCli_ComponentAddReportsTheIdItAssigned", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const auto run = runCliCommand({"component-add", cliPath(path), "--part", "Block", "--name", "Base"});
    REQUIRE(run.exitCode == ExitCode::Success);

    // The reported ID is the one a later command can use as a selector,
    // which is what makes a script able to stop relying on the name.
    const ComponentId base = componentNamed(load(path), "Base");
    CHECK_THAT(run.out, ContainsSubstring(std::format("object:{}", base.value())));
    CHECK(runCliCommand({"component-place", cliPath(path), std::to_string(base.value()), "--x", "1mm"}).exitCode ==
          ExitCode::Success);
}

TEST_CASE("AssemblyCli_ComponentAddNamesUnnamedComponentsDeterministically", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"component-add", file, "--part", "Block"}).exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"component-add", file, "--part", "Block"}).exitCode == ExitCode::Success);

    const Document after = load(path);
    CHECK(after.findObjectByName("Component1") != nullptr);
    CHECK(after.findObjectByName("Component2") != nullptr);
}

TEST_CASE("AssemblyCli_ComponentAddRefusesAPartThatProducesNoBody", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    // A sketch is not a part. The model refuses it, not the CLI, so this is
    // a failure rather than a usage error.
    const auto run = runCliCommand({"component-add", cliPath(path), "--part", "BlockSketch"});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("produces no body"));
    CHECK(assembly::components(load(path)).empty());
}

// --- Placement --------------------------------------------------------------

TEST_CASE("AssemblyCli_ComponentPlaceChangesOnlyTheAxesGiven", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"component-place", file, "Arm", "--x", "12mm", "--rz", "30deg"}).exitCode ==
            ExitCode::Success);

    const ComponentPlacement placement =
        assembly::findComponent(load(path), componentNamed(load(path), "Arm"))->definition().placement;
    CHECK(placement.translation[0].in(units::mm) == 12.0);
    CHECK(placement.translation[1].in(units::mm) == 0.0);
    CHECK(placement.translation[2].in(units::mm) == 50.0); // untouched
    // An angle is held in radians, so degrees -> radians -> degrees passes
    // through pi and does not come back bit-exact (30 arrives as
    // 29.999999999999996). 1e-12 is the well-conditioned double-precision
    // figure CLAUDE.md names; the lengths above need no tolerance because
    // millimetres round-trip exactly through metres.
    CHECK_THAT(placement.rotation[2].in(units::deg), WithinAbs(30.0, 1e-12));
    CHECK(placement.rotation[0].in(units::deg) == 0.0);
}

TEST_CASE("AssemblyCli_ComponentPlaceBindsALengthParameter", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    // "lift" is a parameter name, "50mm" is a quantity, and the two can never
    // be confused: a quantity starts with a digit and a name never does.
    REQUIRE(runCliCommand({"component-place", cliPath(path), "Arm", "--z", "lift"}).exitCode == ExitCode::Success);

    Document after = load(path);
    const ComponentPlacement placement =
        assembly::findComponent(after, componentNamed(after, "Arm"))->definition().placement;
    REQUIRE(placement.translationParameters[2].has_value());
    CHECK(*placement.translationParameters[2] == after.parameters().findByName("lift")->id());

    // The binding drives the solve: 50 mm is what "lift" is worth.
    const assembly::AssemblySolveResult result = solveLikeTheCli(after);
    REQUIRE(result.solved());
    const auto arm = result.transforms.find(componentNamed(after, "Arm"));
    REQUIRE(arm != result.transforms.end());
    CHECK(arm->second.translationPart().z.in(units::mm) == 50.0);
}

TEST_CASE("AssemblyCli_PlacementRejectsAValueOfTheWrongDimension", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string before = readFile(path);
    const auto run = runCliCommand({"component-place", cliPath(path), "Arm", "--z", "1kg"});
    CHECK(run.exitCode == ExitCode::UsageError);
    CHECK_THAT(run.err, ContainsSubstring("has dimension mass; expected length"));
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_PlacementRejectsAParameterThatDoesNotExist", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const auto run = runCliCommand({"component-place", cliPath(path), "Arm", "--z", "nonexistent"});
    // The line read fine; the document has no such parameter.
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("no parameter named 'nonexistent'"));
}

// --- Mates ------------------------------------------------------------------

TEST_CASE("AssemblyCli_MateAddAcceptsEveryMateKind", "[cli][assembly][mate][p13]") {
    // All eleven kinds the model distinguishes, each through the CLI, each
    // with the arguments its kind actually needs.
    struct Case {
        std::string_view type;
        std::vector<std::string> extra;
    };
    const std::vector<Case> cases{
        {"fixed", {"--component", "Base"}},
        {"coincident", {"--a", "Base:origin:xy", "--b", "Arm:origin:xy"}},
        {"concentric", {"--a", "Base:origin:z", "--b", "Arm:origin:z"}},
        {"parallel", {"--a", "Base:origin:xy", "--b", "Arm:origin:xy"}},
        {"perpendicular", {"--a", "Base:origin:xy", "--b", "Arm:origin:yz"}},
        {"distance", {"--a", "Base:origin:xy", "--b", "Arm:origin:xy", "--distance", "25mm"}},
        {"angle", {"--a", "Base:origin:xy", "--b", "Arm:origin:yz", "--angle", "45deg"}},
        {"revolute", {"--a", "Base:origin:z", "--b", "Arm:origin:z"}},
        {"slider", {"--a", "Base:origin:z", "--b", "Arm:origin:z", "--a2", "Base:origin:xy", "--b2",
                    "Arm:origin:xy"}},
        {"cylindrical", {"--a", "Base:origin:z", "--b", "Arm:origin:z"}},
        {"planar", {"--a", "Base:origin:xy", "--b", "Arm:origin:xy"}},
    };
    REQUIRE(cases.size() == 11);

    for (const Case& test : cases) {
        INFO("mate type " << test.type);
        TempDir dir;
        const auto path = twoComponents(dir);
        std::vector<std::string> args{"mate-add", cliPath(path), "--type", std::string{test.type},
                                      "--name",   "Joint"};
        args.insert(args.end(), test.extra.begin(), test.extra.end());
        const auto run = runCliCommand(args);
        REQUIRE(run.exitCode == ExitCode::Success);

        const Document after = load(path);
        const assembly::Mate* mate = assembly::findMate(after, mateNamed(after, "Joint"));
        REQUIRE(mate != nullptr);
        CHECK(toString(mate->definition().type) == test.type);
    }
}

TEST_CASE("AssemblyCli_MateAddRejectsAnUnknownKind", "[cli][assembly][mate][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const auto run = runCliCommand({"mate-add", cliPath(path), "--type", "wobble"});
    CHECK(run.exitCode == ExitCode::UsageError);
    CHECK_THAT(run.err, ContainsSubstring("is not a mate type"));
    // The message teaches the whole set rather than just refusing.
    CHECK_THAT(run.err, ContainsSubstring("cylindrical"));
}

TEST_CASE("AssemblyCli_SliderRefusedWithoutItsRollReference", "[cli][assembly][mate][p13]") {
    // P13-MATE-002 made a slider the one kind that needs a second target
    // pair. The CLI must not be a way round that, and the refusal comes from
    // the model rather than from anything restated here.
    TempDir dir;
    const auto path = twoComponents(dir);
    const auto run = runCliCommand(
        {"mate-add", cliPath(path), "--type", "slider", "--a", "Base:origin:z", "--b", "Arm:origin:z"});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK(assembly::mates(load(path)).size() == 1); // only Ground
}

TEST_CASE("AssemblyCli_MateSetEditsOnlyWhatIsGiven", "[cli][assembly][mate][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "distance", "--name", "Gap", "--a", "Base:origin:xy", "--b",
                           "Arm:origin:xy", "--distance", "25mm"})
                .exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"mate-set", file, "Gap", "--distance", "40mm"}).exitCode == ExitCode::Success);

    const Document after = load(path);
    const assembly::MateDefinition& definition = assembly::findMate(after, mateNamed(after, "Gap"))->definition();
    CHECK(definition.distance->in(units::mm) == 40.0);
    // The targets it did not mention are exactly as they were.
    REQUIRE(definition.a.has_value());
    CHECK(definition.a->component == componentNamed(after, "Base"));
    CHECK(definition.b->component == componentNamed(after, "Arm"));
}

TEST_CASE("AssemblyCli_MateSetRefusesAValueTheKindDoesNotTake", "[cli][assembly][mate][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "coincident", "--name", "Flush", "--a", "Base:origin:xy",
                           "--b", "Arm:origin:xy"})
                .exitCode == ExitCode::Success);
    const std::string before = readFile(path);

    const auto run = runCliCommand({"mate-set", file, "Flush", "--distance", "5mm"});
    // The command line was well formed; the model rejected the combination.
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("distance"));
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_MateRemoveLeavesItsComponents", "[cli][assembly][mate][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    REQUIRE(runCliCommand({"mate-remove", cliPath(path), "Ground"}).exitCode == ExitCode::Success);

    const Document after = load(path);
    CHECK(assembly::mates(after).empty());
    CHECK(assembly::components(after).size() == 2);
}

TEST_CASE("AssemblyCli_ComponentRemoveRefusedWhileMated", "[cli][assembly][p13]") {
    // Removing a mated component would leave a mate naming something that is
    // gone. P13-PERSIST-001 made such a file load and report itself broken,
    // which is right for a file already damaged and wrong to create on
    // purpose.
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string before = readFile(path);
    const auto run = runCliCommand({"component-remove", cliPath(path), "Base"});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("still mated by Ground"));
    CHECK(readFile(path) == before);

    // Remove the mate and the component goes.
    REQUIRE(runCliCommand({"mate-remove", cliPath(path), "Ground"}).exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"component-remove", cliPath(path), "Base"}).exitCode == ExitCode::Success);
    CHECK(assembly::components(load(path)).size() == 1);
}

// --- The mate target grammar ------------------------------------------------

TEST_CASE("AssemblyCli_MateTargetGrammarRoundTrips", "[cli][assembly][mate][p13]") {
    // What status prints can be pasted back into a command. Asserted by
    // building the mate from the text and reading the text back out of the
    // saved document, so the two directions are checked against each other
    // rather than against one function's own output.
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{
             {"Base:origin:xy", "Arm:origin:xy"},
             {"Base:origin:yz", "Arm:origin:yz"},
             {"Base:origin:xz", "Arm:origin:xz"},
             {"Base:face:Block:end_cap", "Arm:face:Block:end_cap"},
             {"Base:face:Block:start_cap", "Arm:face:Block:start_cap"},
         }) {
        INFO("target " << a << " to " << b);
        REQUIRE(runCliCommand({"mate-add", file, "--type", "parallel", "--name", "Probe", "--a", a, "--b", b})
                    .exitCode == ExitCode::Success);
        const auto status = runCliCommand({"status", file});
        CHECK_THAT(status.out, ContainsSubstring(std::format("{} to {}", a, b)));
        REQUIRE(runCliCommand({"mate-remove", file, "Probe"}).exitCode == ExitCode::Success);
    }
}

TEST_CASE("AssemblyCli_MateTargetRejectsUnknownGeometry", "[cli][assembly][mate][p13]") {
    TempDir dir;
    const std::string file = cliPath(twoComponents(dir));
    for (const auto& [target, expected] : std::vector<std::pair<std::string, std::string>>{
             {"Base:origin:nope", "is not a plane"},
             {"Base:wibble:xy", "is not a geometry kind"},
             {"Base", "is not a mate target"},
             {"Base:face:Block:not_a_role", "is not a face role"},
             {"Base:face:Block:end_cap:5", "only a side face is named by a profile entity"},
         }) {
        INFO("target " << target);
        const auto run =
            runCliCommand({"mate-add", file, "--type", "parallel", "--a", target, "--b", "Arm:origin:xy"});
        CHECK(run.exitCode == ExitCode::UsageError);
        CHECK_THAT(run.err, ContainsSubstring(expected));
    }
}

TEST_CASE("AssemblyCli_MateTargetOnAMissingComponentIsAFailureNotAUsageError", "[cli][assembly][mate][p13]") {
    TempDir dir;
    const std::string file = cliPath(twoComponents(dir));
    const auto run =
        runCliCommand({"mate-add", file, "--type", "parallel", "--a", "Ghost:origin:xy", "--b", "Arm:origin:xy"});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("nothing named 'Ghost'"));
}

// --- Configurations and suppression -----------------------------------------

TEST_CASE("AssemblyCli_SuppressionHasThreeStatesInAConfiguration", "[cli][assembly][configuration][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"configuration-add", file, "Stripped"}).exitCode == ExitCode::Success);

    const auto overrideFor = [&](std::string_view configuration) {
        const Document document = load(path);
        const Configuration* found = document.configurations().findByName(configuration);
        REQUIRE(found != nullptr);
        return found->suppressionFor(componentNamed(document, "Arm"));
    };

    CHECK_FALSE(overrideFor("Stripped").has_value()); // no opinion
    REQUIRE(runCliCommand({"suppress", file, "Arm", "--configuration", "Stripped"}).exitCode == ExitCode::Success);
    CHECK(overrideFor("Stripped") == std::optional{true});
    REQUIRE(runCliCommand({"unsuppress", file, "Arm", "--configuration", "Stripped"}).exitCode == ExitCode::Success);
    CHECK(overrideFor("Stripped") == std::optional{false}); // "in this build, present"
    REQUIRE(runCliCommand({"suppress-clear", file, "Arm", "--configuration", "Stripped"}).exitCode ==
            ExitCode::Success);
    CHECK_FALSE(overrideFor("Stripped").has_value()); // back to no opinion
}

TEST_CASE("AssemblyCli_BaseSuppressionIsDistinctFromAConfigurationOverride", "[cli][assembly][configuration][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"configuration-add", file, "Full"}).exitCode == ExitCode::Success);
    // Suppressed at the base...
    REQUIRE(runCliCommand({"suppress", file, "Arm"}).exitCode == ExitCode::Success);
    {
        const Document document = load(path);
        CHECK(assembly::findComponent(document, componentNamed(document, "Arm"))->definition().suppressed);
        CHECK(assembly::isComponentSuppressed(document, componentNamed(document, "Arm")));
    }
    // ...and brought back by one configuration's opinion of it.
    REQUIRE(runCliCommand({"unsuppress", file, "Arm", "--configuration", "Full"}).exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"configuration-activate", file, "Full"}).exitCode == ExitCode::Success);
    {
        const Document document = load(path);
        CHECK(assembly::findComponent(document, componentNamed(document, "Arm"))->definition().suppressed);
        CHECK_FALSE(assembly::isComponentSuppressed(document, componentNamed(document, "Arm")));
    }
}

TEST_CASE("AssemblyCli_SuppressClearNeedsAConfiguration", "[cli][assembly][configuration][p13]") {
    TempDir dir;
    const auto run = runCliCommand({"suppress-clear", cliPath(twoComponents(dir)), "Arm"});
    CHECK(run.exitCode == ExitCode::UsageError);
    CHECK_THAT(run.err, ContainsSubstring("only a configuration's override can be cleared"));
}

TEST_CASE("AssemblyCli_SuppressRefusesSomethingThatIsNeitherComponentNorMate", "[cli][assembly][p13]") {
    TempDir dir;
    const auto run = runCliCommand({"suppress", cliPath(twoComponents(dir)), "Block"});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("only a component or a mate is suppressed"));
}

TEST_CASE("AssemblyCli_ConfigurationActivateSelectsAndClears", "[cli][assembly][configuration][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"configuration-add", file, "Large"}).exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"configuration-activate", file, "Large"}).exitCode == ExitCode::Success);
    {
        const Document document = load(path);
        REQUIRE(document.activeConfiguration().has_value());
        CHECK(document.configurations().find(*document.activeConfiguration())->name() == "Large");
    }
    REQUIRE(runCliCommand({"configuration-activate", file, "--none"}).exitCode == ExitCode::Success);
    CHECK_FALSE(load(path).activeConfiguration().has_value());

    const auto unknown = runCliCommand({"configuration-activate", file, "Enormous"});
    CHECK(unknown.exitCode == ExitCode::Failure);
    CHECK_THAT(unknown.err, ContainsSubstring("no configuration named 'Enormous'"));
}

// --- Regenerate, solve and status -------------------------------------------

TEST_CASE("AssemblyCli_SolveReportsStatusAndDegreesOfFreedom", "[cli][assembly][solve][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    // Grounded Base, and Arm free: 6 unknowns, no equations.
    {
        const auto run = runCliCommand({"solve", file});
        CHECK(run.exitCode == ExitCode::Success);
        CHECK_THAT(run.out, ContainsSubstring("UNDER_CONSTRAINED"));
        CHECK_THAT(run.out, ContainsSubstring("6 degrees of freedom"));
    }
    // A plane-to-plane coincidence removes three: one translation and two
    // rotations. Hand-derived, not read back from the solver.
    REQUIRE(runCliCommand({"mate-add", file, "--type", "coincident", "--name", "Flush", "--a", "Base:origin:xy",
                           "--b", "Arm:origin:xy"})
                .exitCode == ExitCode::Success);
    {
        const auto run = runCliCommand({"solve", file});
        CHECK(run.exitCode == ExitCode::Success);
        CHECK_THAT(run.out, ContainsSubstring("3 degrees of freedom"));
        // The mate pulls Arm from its 50 mm intent down onto Base's plane.
        CHECK_THAT(run.out, ContainsSubstring("origin (0, 0, 0) mm"));
    }
}

TEST_CASE("AssemblyCli_SolveReportsAnInconsistentAssemblyAsAFailure", "[cli][assembly][solve][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    // Two distances that cannot both hold.
    REQUIRE(runCliCommand({"mate-add", file, "--type", "distance", "--name", "Near", "--a", "Base:origin:xy", "--b",
                           "Arm:origin:xy", "--distance", "10mm"})
                .exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "distance", "--name", "Far", "--a", "Base:origin:xy", "--b",
                           "Arm:origin:xy", "--distance", "90mm"})
                .exitCode == ExitCode::Success);

    const auto run = runCliCommand({"solve", file});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.out, ContainsSubstring("INCONSISTENT"));
    // A failed solve names the mates that cannot hold, rather than reducing
    // the answer to a boolean.
    CHECK_THAT(run.out, ContainsSubstring("conflicting"));
}

TEST_CASE("AssemblyCli_StatusListsComponentsMatesAndSuppression", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"configuration-add", file, "Stripped"}).exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"suppress", file, "Ground", "--configuration", "Stripped"}).exitCode ==
            ExitCode::Success);

    const auto base = runCliCommand({"status", file});
    CHECK(base.exitCode == ExitCode::Success);
    CHECK_THAT(base.out, ContainsSubstring("Components (2)"));
    CHECK_THAT(base.out, ContainsSubstring("Mates (1)"));
    CHECK_THAT(base.out, ContainsSubstring("Configuration: none (base values)"));
    CHECK_THAT(base.out, ContainsSubstring("Ground"));

    const auto stripped = runCliCommand({"status", file, "--configuration", "Stripped"});
    CHECK(stripped.exitCode == ExitCode::Success);
    CHECK_THAT(stripped.out, ContainsSubstring("Configuration: Stripped"));
    CHECK_THAT(stripped.out, ContainsSubstring("suppressed"));
}

TEST_CASE("AssemblyCli_SolveHonoursTheChosenConfiguration", "[cli][assembly][configuration][solve][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "coincident", "--name", "Flush", "--a", "Base:origin:xy",
                           "--b", "Arm:origin:xy"})
                .exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"configuration-add", file, "Loose"}).exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"suppress", file, "Flush", "--configuration", "Loose"}).exitCode == ExitCode::Success);

    CHECK_THAT(runCliCommand({"solve", file}).out, ContainsSubstring("3 degrees of freedom"));
    // With the mate suppressed the constraint is gone, and Arm sits at the
    // intent that was never overwritten -- ADR-005, seen from the outside.
    const auto loose = runCliCommand({"solve", file, "--configuration", "Loose"});
    CHECK_THAT(loose.out, ContainsSubstring("6 degrees of freedom"));
    CHECK_THAT(loose.out, ContainsSubstring("origin (0, 0, 50) mm"));
}

TEST_CASE("AssemblyCli_RegenerateAndSolveAndStatusWriteNothing", "[cli][assembly][p13]") {
    // ADR-005: a transform is derived, so there is nothing for these to
    // persist. Checked on the bytes, because "it reported success" is not
    // evidence that it wrote nothing.
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "coincident", "--name", "Flush", "--a", "Base:origin:xy",
                           "--b", "Arm:origin:xy"})
                .exitCode == ExitCode::Success);
    const std::string before = readFile(path);

    for (const std::string_view command : {"regenerate", "solve", "status"}) {
        INFO("command " << command);
        CHECK(runCliCommand({std::string{command}, file}).exitCode == ExitCode::Success);
        CHECK(readFile(path) == before);
    }
}

TEST_CASE("AssemblyCli_SolveOnADocumentWithNoComponentsIsNotAFailure", "[cli][assembly][solve][p13]") {
    TempDir dir;
    const auto run = runCliCommand({"solve", cliPath(savedPart(dir))});
    CHECK(run.exitCode == ExitCode::Success);
    CHECK_THAT(run.out, ContainsSubstring("no components"));
}

// --- Scripted workflows -----------------------------------------------------

TEST_CASE("AssemblyCli_BatchAppliesEveryEditAsOneTransaction", "[cli][assembly][batch][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const auto script = dir.path() / "build.txt";
    writeScript(script, {"# a two-part assembly, built headless", "",
                         "component-add --part Block --name Base",
                         "component-add --part Block --name Arm --z 50mm",
                         "mate-add --type fixed --component Base --name Ground",
                         "mate-add --type coincident --a Base:origin:xy --b Arm:origin:xy --name Flush"});

    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    REQUIRE(run.exitCode == ExitCode::Success);
    CHECK_THAT(run.out, ContainsSubstring("Applied 4 edits"));

    const Document after = load(path);
    CHECK(assembly::components(after).size() == 2);
    CHECK(assembly::mates(after).size() == 2);
}

TEST_CASE("AssemblyCli_BatchThatFailsWritesNothingAtAll", "[cli][assembly][batch][p13]") {
    // THE case this milestone exists for. Three edits succeed, the fourth
    // fails, and the file must be byte-for-byte what it was -- otherwise a
    // rerun of the corrected script applies the first three twice.
    TempDir dir;
    const auto path = savedPart(dir);
    const std::string before = readFile(path);
    const auto script = dir.path() / "bad.txt";
    writeScript(script, {"component-add --part Block --name Base",
                         "component-add --part Block --name Arm --z 50mm",
                         "component-add --part Block --name Third",
                         "mate-add --type fixed --component Nonexistent --name Ground",
                         "component-add --part Block --name Fourth"});

    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK(readFile(path) == before);
    CHECK(assembly::components(load(path)).empty());
    // The diagnostic names the line, and says plainly that nothing was
    // written -- a script driver should not have to infer it.
    CHECK_THAT(run.err, ContainsSubstring("bad.txt:4:"));
    CHECK_THAT(run.err, ContainsSubstring("is unchanged"));
    // And it reports no edits as applied, so the list cannot be mistaken for
    // work that happened.
    CHECK(run.out.empty());
}

TEST_CASE("AssemblyCli_BatchDryRunChecksWithoutWriting", "[cli][assembly][batch][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const std::string before = readFile(path);
    const auto script = dir.path() / "build.txt";
    writeScript(script, {"component-add --part Block --name Base"});

    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script), "--dry-run"});
    CHECK(run.exitCode == ExitCode::Success);
    CHECK_THAT(run.out, ContainsSubstring("Checked 1 edit"));
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_BatchSkipsCommentsAndBlankLines", "[cli][assembly][batch][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const auto script = dir.path() / "comments.txt";
    writeScript(script, {"# leading comment", "", "   ",
                         "component-add --part Block --name Base   # trailing comment", "\t",
                         "# component-add --part Block --name NotThisOne"});

    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    REQUIRE(run.exitCode == ExitCode::Success);
    CHECK_THAT(run.out, ContainsSubstring("Applied 1 edit"));
    const Document after = load(path);
    CHECK(assembly::components(after).size() == 1);
    CHECK(after.findObjectByName("NotThisOne") == nullptr);
}

TEST_CASE("AssemblyCli_BatchHonoursQuotedArguments", "[cli][assembly][batch][p13]") {
    // What quoting is actually for here. Every NAME in this model is an
    // identifier -- Configurations.hpp says so in as many words, "so that
    // they can be typed on a command line without quoting" -- so no name
    // ever needs it. A QUANTITY does: "50 mm" with a space before the unit
    // is accepted, and without quotes the tokeniser would see two arguments.
    TempDir dir;
    const auto path = savedPart(dir);
    const auto script = dir.path() / "quoted.txt";
    writeScript(script, {"component-add --part Block --name Base --z \"50 mm\""});
    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    REQUIRE(run.exitCode == ExitCode::Success);

    const Document after = load(path);
    CHECK(assembly::findComponent(after, componentNamed(after, "Base"))
              ->definition()
              .placement.translation[2]
              .in(units::mm) == 50.0);

    // And without the quotes the same line is refused, rather than silently
    // taking "50" and leaving "mm" as a stray argument.
    const auto unquoted = dir.path() / "unquoted.txt";
    writeScript(unquoted, {"component-add --part Block --name Other --z 50 mm"});
    CHECK(runCliCommand({"batch", cliPath(path), cliPath(unquoted)}).exitCode == ExitCode::UsageError);
}

TEST_CASE("AssemblyCli_BatchRejectsAnUnterminatedQuote", "[cli][assembly][batch][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const std::string before = readFile(path);
    const auto script = dir.path() / "broken.txt";
    writeScript(script, {"component-add --part Block --name Base", "configuration-add \"unfinished"});

    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("line 2: unterminated"));
    // The script is refused before any edit runs, so nothing is written.
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_BatchRejectsAnUnknownEdit", "[cli][assembly][batch][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const std::string before = readFile(path);
    const auto script = dir.path() / "unknown.txt";
    writeScript(script, {"component-add --part Block --name Base", "frobnicate --hard"});

    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("'frobnicate' is not an edit"));
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_BatchRejectsAReportCommandAsAnEdit", "[cli][assembly][batch][p13]") {
    // A batch is a transaction of edits. "solve" is not an edit, and a script
    // that contains one is refused rather than silently ignored.
    TempDir dir;
    const auto path = savedPart(dir);
    const auto script = dir.path() / "mixed.txt";
    writeScript(script, {"component-add --part Block --name Base", "solve"});
    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("'solve' is not an edit"));
}

TEST_CASE("AssemblyCli_BatchReportsAUsageErrorFromAScriptLine", "[cli][assembly][batch][p13]") {
    TempDir dir;
    const auto path = savedPart(dir);
    const auto script = dir.path() / "usage.txt";
    writeScript(script, {"component-add --part Block --name Base --z 1kg"});
    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    // A malformed line keeps its own exit code through the batch driver.
    CHECK(run.exitCode == ExitCode::UsageError);
    CHECK_THAT(run.err, ContainsSubstring("usage.txt:1:"));
}

TEST_CASE("AssemblyCli_BatchSeesItsOwnEarlierEdits", "[cli][assembly][batch][p13]") {
    // The claim "a batch is ONE transaction over ONE document" is only true
    // if a later line sees what an earlier line did. If each line reloaded
    // the file, this script could not work at all: line 2 names a component
    // line 1 created, line 3 moves it, and line 4 mates the moved one.
    TempDir dir;
    const auto path = savedPart(dir);
    const auto script = dir.path() / "sequential.txt";
    writeScript(script, {"component-add --part Block --name Base",
                         "component-add --part Block --name Arm",
                         "component-place Arm --z 50mm",
                         "component-place Arm --x 12mm",
                         "mate-add --type fixed --component Base --name Ground",
                         "mate-add --type coincident --a Base:origin:xy --b Arm:origin:xy --name Flush"});
    REQUIRE(runCliCommand({"batch", cliPath(path), cliPath(script)}).exitCode == ExitCode::Success);

    const Document after = load(path);
    const ComponentPlacement placement =
        assembly::findComponent(after, componentNamed(after, "Arm"))->definition().placement;
    // Both placements applied, to the same component, in order.
    CHECK(placement.translation[0].in(units::mm) == 12.0);
    CHECK(placement.translation[2].in(units::mm) == 50.0);
    CHECK(assembly::mates(after).size() == 2);
}

TEST_CASE("AssemblyCli_BatchCanRemoveWhatItCreated", "[cli][assembly][batch][p13]") {
    // The other direction of the same property, and the one that would break
    // if a removal were deferred to the save rather than applied in place.
    TempDir dir;
    const auto path = savedPart(dir);
    const auto script = dir.path() / "churn.txt";
    writeScript(script, {"component-add --part Block --name Scratch",
                         "component-add --part Block --name Keep",
                         "component-remove Scratch",
                         // The name is free again, so it can be reused.
                         "component-add --part Block --name Scratch"});
    REQUIRE(runCliCommand({"batch", cliPath(path), cliPath(script)}).exitCode == ExitCode::Success);

    const Document after = load(path);
    CHECK(assembly::components(after).size() == 2);
    CHECK(after.findObjectByName("Keep") != nullptr);
    CHECK(after.findObjectByName("Scratch") != nullptr);
}

TEST_CASE("AssemblyCli_BatchWithNoEditsIsNotAFailure", "[cli][assembly][batch][p13]") {
    // A script of nothing but comments is a legitimate thing to run -- it is
    // what a half-commented script becomes -- and it must not be an error.
    TempDir dir;
    const auto path = savedPart(dir);
    const std::string before = readFile(path);
    const auto script = dir.path() / "empty.txt";
    writeScript(script, {"# nothing to do today", ""});
    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    CHECK(run.exitCode == ExitCode::Success);
    CHECK_THAT(run.out, ContainsSubstring("0 edits"));
    // And it is still byte-identical, because the document it wrote is the
    // one it read and serialization is canonical (P13-PERSIST-001).
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_AnEditThatChangesNothingLeavesTheBytesUnchanged", "[cli][assembly][p13]") {
    // An edit that asks for what is already true still saves, so this is
    // worth pinning rather than assuming: canonical serialization means the
    // rewrite is byte-for-byte the same file, and a script that is run twice
    // does not churn the document.
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"component-place", file, "Arm", "--x", "7mm"}).exitCode == ExitCode::Success);
    const std::string once = readFile(path);
    REQUIRE(runCliCommand({"component-place", file, "Arm", "--x", "7mm"}).exitCode == ExitCode::Success);
    CHECK(readFile(path) == once);
}

// --- The single-shot form is the batch of one -------------------------------

TEST_CASE("AssemblyCli_AFailedSingleEditWritesNothing", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string before = readFile(path);
    CHECK(runCliCommand({"mate-add", cliPath(path), "--type", "distance", "--a", "Base:origin:xy", "--b",
                         "Arm:origin:xy"})
              .exitCode == ExitCode::Failure); // a distance mate needs a distance
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_SingleShotAndBatchProduceTheSameDocument", "[cli][assembly][batch][p13]") {
    // ADR-009 says the single-shot command IS the batch of one, through the
    // same EditApply. If that ever stopped being true, the two would drift.
    TempDir dir;
    const auto stepwise = savedPart(dir, "stepwise.bcad");
    const auto scripted = savedPart(dir, "scripted.bcad");

    const std::vector<std::vector<std::string>> edits{
        {"component-add", "--part", "Block", "--name", "Base"},
        {"component-add", "--part", "Block", "--name", "Arm", "--z", "50mm"},
        {"mate-add", "--type", "fixed", "--component", "Base", "--name", "Ground"},
        {"mate-add", "--type", "coincident", "--a", "Base:origin:xy", "--b", "Arm:origin:xy", "--name", "Flush"},
    };
    std::vector<std::string> lines;
    for (const std::vector<std::string>& edit : edits) {
        std::vector<std::string> args{edit.front(), cliPath(stepwise)};
        args.insert(args.end(), edit.begin() + 1, edit.end());
        REQUIRE(runCliCommand(args).exitCode == ExitCode::Success);

        std::string line;
        for (const std::string& token : edit) {
            line += line.empty() ? "" : " ";
            line += token;
        }
        lines.push_back(line);
    }
    const auto script = dir.path() / "same.txt";
    writeScript(script, lines);
    REQUIRE(runCliCommand({"batch", cliPath(scripted), cliPath(script)}).exitCode == ExitCode::Success);

    // The documents differ only in their identity, so compare what the
    // assembly is rather than the whole document.
    const Document a = load(stepwise);
    const Document b = load(scripted);
    REQUIRE(assembly::components(a) == assembly::components(b));
    REQUIRE(assembly::mates(a) == assembly::mates(b));
    for (const ComponentId id : assembly::components(a)) {
        CHECK(assembly::findComponent(a, id)->definition() == assembly::findComponent(b, id)->definition());
    }
    for (const MateId id : assembly::mates(a)) {
        CHECK(assembly::findMate(a, id)->definition() == assembly::findMate(b, id)->definition());
    }
}

// --- CLI / core equivalence -------------------------------------------------

TEST_CASE("AssemblyCli_BuildsTheSameAssemblyAsTheCoreApi", "[cli][assembly][p13]") {
    // The gate's "CLI/core equivalence", in the strongest form available
    // since P13-PERSIST-001: build the assembly both ways from the same
    // starting document, and compare BOTH the canonical intent and the
    // transforms they solve to -- bit for bit, not within a tolerance.
    TempDir dir;
    const PartDocument part = makePart();
    const auto viaCli = save(dir, part.document, "cli.bcad");
    const auto viaCore = save(dir, part.document, "core.bcad");
    const std::string file = cliPath(viaCli);

    // Through the CLI.
    REQUIRE(runCliCommand({"component-add", file, "--part", "Block", "--name", "Base"}).exitCode ==
            ExitCode::Success);
    REQUIRE(runCliCommand({"component-add", file, "--part", "Block", "--name", "Arm", "--z", "50mm"}).exitCode ==
            ExitCode::Success);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "fixed", "--component", "Base", "--name", "Ground"})
                .exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "coincident", "--name", "Flush", "--a", "Base:origin:xy",
                           "--b", "Arm:origin:xy"})
                .exitCode == ExitCode::Success);

    // The same, in process, through the commands the CLI routes through.
    {
        Document document = load(viaCore);
        const ObjectId block = document.findObjectByName("Block")->id();
        assembly::CreateComponentCommand base{"Base", {.part = ObjectReference{block}}};
        REQUIRE(base.execute(document).has_value());
        ComponentPlacement lifted;
        lifted.translation[2] = 50_mm;
        assembly::CreateComponentCommand arm{"Arm", {.part = ObjectReference{block}, .placement = lifted}};
        REQUIRE(arm.execute(document).has_value());
        assembly::CreateMateCommand ground{
            "Ground", {.type = assembly::MateType::Fixed, .component = base.componentId()}};
        REQUIRE(ground.execute(document).has_value());
        assembly::CreateMateCommand flush{
            "Flush",
            {.type = assembly::MateType::Coincident,
             .a = planeTarget(base.componentId(), PlaneReference{.plane = PrincipalPlane::XY}),
             .b = planeTarget(arm.componentId(), PlaneReference{.plane = PrincipalPlane::XY})}};
        REQUIRE(flush.execute(document).has_value());
        REQUIRE(io::saveDocument(document, viaCore).has_value());
    }

    Document fromCli = load(viaCli);
    Document fromCore = load(viaCore);
    // Same identity, name, metadata, parameters, configurations and objects:
    // a *nearly* identical document would fail here.
    CHECK(equivalent(fromCli, fromCore));
    // And the same answer, exactly.
    const assembly::AssemblySolveResult cliResult = solveLikeTheCli(fromCli);
    const assembly::AssemblySolveResult coreResult = solveLikeTheCli(fromCore);
    REQUIRE(cliResult.solved());
    CHECK(cliResult.status == coreResult.status);
    CHECK(cliResult.degreesOfFreedom == coreResult.degreesOfFreedom);
    REQUIRE(cliResult.transforms.size() == coreResult.transforms.size());
    for (const auto& [component, transform] : cliResult.transforms) {
        const auto other = coreResult.transforms.find(component);
        REQUIRE(other != coreResult.transforms.end());
        CHECK(transform.matrix() == other->second.matrix());
        CHECK(transform.translationPart().x.si() == other->second.translationPart().x.si());
        CHECK(transform.translationPart().y.si() == other->second.translationPart().y.si());
        CHECK(transform.translationPart().z.si() == other->second.translationPart().z.si());
    }
}

TEST_CASE("AssemblyCli_RefusesANameAlreadyInUse", "[cli][assembly][p13]") {
    // A name is unique across objects AND parameters, which is what lets the
    // CLI resolve one to an ID at all. The document enforces it; the CLI must
    // not have a way round it.
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    const std::string before = readFile(path);

    const auto sameComponent = runCliCommand({"component-add", file, "--part", "Block", "--name", "Arm"});
    CHECK(sameComponent.exitCode == ExitCode::Failure);
    // Even a name taken by a PARAMETER is refused, because they share one
    // space -- which is exactly why a lookup by name is unambiguous.
    const auto sameParameter = runCliCommand({"component-add", file, "--part", "Block", "--name", "lift"});
    CHECK(sameParameter.exitCode == ExitCode::Failure);
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_SaveLoadRegenerateSolveRoundTripsExactly", "[cli][assembly][solve][p13]") {
    // The workflow the gate names, end to end: a document the CLI built is
    // saved, loaded again, regenerated and solved -- and answers the same
    // thing bit for bit. P13-PERSIST-001 made this checkable exactly rather
    // than within a tolerance.
    TempDir dir;
    const auto path = twoComponents(dir);
    const std::string file = cliPath(path);
    REQUIRE(runCliCommand({"mate-add", file, "--type", "coincident", "--name", "Flush", "--a", "Base:origin:xy",
                           "--b", "Arm:origin:xy"})
                .exitCode == ExitCode::Success);

    Document first = load(path);
    const assembly::AssemblySolveResult before = solveLikeTheCli(first);
    REQUIRE(before.status == assembly::SolveStatus::UnderConstrained);
    REQUIRE(before.degreesOfFreedom == 3);

    // Save it again from the loaded copy, then load THAT and solve.
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(first, again).has_value());
    Document second = load(again);
    const assembly::AssemblySolveResult after = solveLikeTheCli(second);

    CHECK(after.status == before.status);
    CHECK(after.degreesOfFreedom == before.degreesOfFreedom);
    CHECK(after.equations == before.equations);
    REQUIRE(after.transforms.size() == before.transforms.size());
    for (const auto& [component, transform] : before.transforms) {
        const auto other = after.transforms.find(component);
        REQUIRE(other != after.transforms.end());
        CHECK(transform.matrix() == other->second.matrix());
        CHECK(transform.translationPart().z.si() == other->second.translationPart().z.si());
    }
    // And the file itself is canonical: writing the loaded document gives
    // back the same bytes.
    CHECK(readFile(again) == readFile(path));
}

TEST_CASE("AssemblyCli_ReportsAnAssemblyWhosePartWentMissing", "[cli][assembly][p13]") {
    // A broken reference is reported rather than hidden, and it is a
    // non-zero exit so a script driver can react to it.
    TempDir dir;
    const auto path = twoComponents(dir);
    {
        Document document = load(path);
        // Remove the part both components place, the way a careless edit
        // through another client might.
        REQUIRE(document.removeObject(document.findObjectByName("Block")->id()).has_value());
        REQUIRE(io::saveDocument(document, path).has_value());
    }
    const auto run = runCliCommand({"status", cliPath(path)});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.out, ContainsSubstring("cannot resolve its part"));
}

// --- Malformed input --------------------------------------------------------

TEST_CASE("AssemblyCli_RejectsAFileThatIsNotThere", "[cli][assembly][p13]") {
    TempDir dir;
    const std::string missing = cliPath(dir.path() / "not-here.bcad");
    for (const std::string_view command : {"status", "solve", "regenerate"}) {
        INFO("command " << command);
        CHECK(runCliCommand({std::string{command}, missing}).exitCode == ExitCode::Failure);
    }
    CHECK(runCliCommand({"component-add", missing, "--part", "Block"}).exitCode == ExitCode::Failure);
}

TEST_CASE("AssemblyCli_RejectsACorruptFileWithoutWritingIt", "[cli][assembly][p13]") {
    TempDir dir;
    const auto path = dir.path() / "corrupt.bcad";
    {
        std::ofstream file(path, std::ios::binary);
        file << "{ \"format\": \"bettercad-document\", this is not json";
    }
    const std::string before = readFile(path);
    const auto run = runCliCommand({"component-add", cliPath(path), "--part", "Block"});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_FALSE(run.err.empty());
    CHECK(readFile(path) == before);
}

TEST_CASE("AssemblyCli_EditsNeedADocument", "[cli][assembly][p13]") {
    for (const std::string_view command :
         {"component-add", "component-place", "component-remove", "mate-add", "mate-set", "mate-remove", "suppress",
          "unsuppress", "suppress-clear", "configuration-add", "configuration-activate"}) {
        INFO("command " << command);
        const auto run = runCliCommand({std::string{command}});
        CHECK(run.exitCode == ExitCode::UsageError);
        CHECK_THAT(run.err, ContainsSubstring("expected a document file"));
    }
}

TEST_CASE("AssemblyCli_HelpListsEveryAssemblyCommand", "[cli][assembly][p13]") {
    const auto run = runCliCommand({"--help"});
    REQUIRE(run.exitCode == ExitCode::Success);
    for (const std::string_view command :
         {"component-add", "component-place", "component-remove", "mate-add", "mate-set", "mate-remove", "suppress",
          "unsuppress", "suppress-clear", "configuration-add", "configuration-activate", "regenerate", "solve",
          "status", "batch"}) {
        INFO("command " << command);
        CHECK_THAT(run.out, ContainsSubstring(std::string{command}));
    }
}
