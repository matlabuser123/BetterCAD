// P16-CLI-001: headless meshing.
//
// WHAT THESE TESTS ARE FOR. The commands are adapters, so the thing worth
// proving is not that they print something but that they print the SAME thing
// the core would say, that they fail when the core fails, and that the exit
// status carries the gate. A test that only checked "exit 0 and some words"
// would pass against a CLI that had quietly grown its own default size.
//
// THE EQUIVALENCE TESTS RUN THE CORE IN PROCESS AND COMPARE. For the stronger
// form -- the real executable in a separate process -- see the process tests
// registered in tests/CMakeLists.txt (cli.mesh.*) and the end-to-end script,
// which is where argument parsing, document loading, runtime deployment and
// the exit code are all actually exercised.

#include "CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

namespace {

/// A block, saved. Asymmetric so no two faces are interchangeable.
[[nodiscard]] std::filesystem::path writeBlock(const TempDir& temp, std::string_view name) {
    Document document{"Part"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    (void)addRectangle(*sketch, 0_mm, 0_mm, 30_mm, 20_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    (void)require(document.addObject(std::move(*extrude)));

    const std::filesystem::path file = temp.path() / std::format("{}.bcad", name);
    const Result<void> saved = io::saveDocument(document, file);
    INFO((saved ? std::string{} : saved.error().message));
    REQUIRE(saved.has_value());
    return file;
}

/// A cylinder, saved. The fixture that can show sizing at all: a planar
/// boundary is exactly representable, so a block's element count barely
/// responds to a target size.
[[nodiscard]] std::filesystem::path writeTube(const TempDir& temp, std::string_view name) {
    Document document{"Tube"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, 6_mm).has_value());
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
    REQUIRE(extrude.has_value());
    (void)require(document.addObject(std::move(*extrude)));

    const std::filesystem::path file = temp.path() / std::format("{}.bcad", name);
    REQUIRE(io::saveDocument(document, file).has_value());
    return file;
}

/// A document with no body at all, which is the missing-geometry fixture.
[[nodiscard]] std::filesystem::path writeEmpty(const TempDir& temp) {
    Document document{"Empty"};
    const std::filesystem::path file = temp.path() / "empty.bcad";
    REQUIRE(io::saveDocument(document, file).has_value());
    return file;
}

[[nodiscard]] std::string path(const std::filesystem::path& file) { return cliPath(file); }

/// One field out of a `--json` payload, by exact key. Deliberately crude: a
/// test that pulled in a JSON parser would be testing the parser, and what
/// matters here is that the field is present with the value the core has.
[[nodiscard]] std::string field(const std::string& json, std::string_view key) {
    const std::string needle = std::format("\"{}\": ", key);
    const std::size_t at = json.find(needle);
    INFO("looking for " << key << " in\n" << json);
    REQUIRE(at != std::string::npos);
    const std::size_t from = at + needle.size();
    const std::size_t end = json.find_first_of(",\n", from);
    REQUIRE(end != std::string::npos);
    return json.substr(from, end - from);
}

/// What the CORE says about a document, computed in process, for the
/// equivalence tests to compare against.
struct CoreResult {
    std::size_t nodes = 0;
    std::size_t elements = 0;
    std::size_t boundaryTriangles = 0;
    double volumeSi = 0.0;
    std::size_t invalid = 0;
    std::size_t warnings = 0;
    std::size_t failures = 0;
    bool dataValid = false;
};

[[nodiscard]] CoreResult viaCore(const std::filesystem::path& file) {
    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    features::Regenerator regenerator;
    requireReport(regenerator, *loaded);
    const std::vector<MeshControlId> controls = meshing::meshControls(*loaded);
    REQUIRE(controls.size() == 1U);

    meshing::Mesher mesher;
    const Result<const meshing::VolumeMesh*> mesh =
        mesher.generate(*loaded, regenerator, controls.front());
    INFO((mesh ? std::string{} : mesh.error().message));
    REQUIRE(mesh.has_value());
    const meshing::MeshQualityReport* quality = mesher.quality(controls.front());
    REQUIRE(quality != nullptr);

    return CoreResult{
        .nodes = (*mesh)->mesh().nodeCount(),
        .elements = (*mesh)->mesh().tetrahedra().size(),
        .boundaryTriangles = (*mesh)->mesh().triangles().size(),
        .volumeSi = (*mesh)->tetrahedralVolume().si(),
        .invalid = quality->invalidElements,
        .warnings = quality->warningElements,
        .failures = quality->failureElements,
        .dataValid = meshing::validate((*mesh)->mesh()).dataValid(),
    };
}

} // namespace

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_SettingsReportsCanonicalIntent", "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "settings");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "8mm"}).exitCode ==
            cli::ExitCode::Success);
    REQUIRE(runCliCommand({"mesh-local-add", path(file), "face:Solid:end_cap", "2mm"}).exitCode ==
            cli::ExitCode::Success);

    const CliRun human = runCliCommand({"mesh-settings", path(file)});
    CHECK(human.exitCode == cli::ExitCode::Success);
    CHECK_THAT(human.out, ContainsSubstring("Global target size: 8 mm"));
    // THE FACE, WRITTEN AS A SELECTOR, so the output can be fed back in.
    CHECK_THAT(human.out, ContainsSubstring("face:Solid:end_cap"));

    const CliRun json = runCliCommand({"mesh-settings", path(file), "--json"});
    CHECK(json.exitCode == cli::ExitCode::Success);
    // The size as a value AND a unit. A bare 0.008 is the shape of a
    // factor-of-1000 mistake nothing downstream could catch.
    CHECK_THAT(json.out, ContainsSubstring("\"globalTargetSize\""));
    CHECK_THAT(json.out, ContainsSubstring("\"value\": 0.008"));
    CHECK_THAT(json.out, ContainsSubstring("\"unit\": \"m\""));
    CHECK_THAT(json.out, ContainsSubstring("\"reference\": \"face:Solid:end_cap\""));
}

TEST_CASE("MeshingCli_SettingsReportsTheAbsenceOfAGlobalSize", "[cli][meshing][meshcli]") {
    // ABSENT IS A STATE: BetterCAD's scale-relative default. The CLI must not
    // print a number for it, because it would be inventing one.
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "default");
    REQUIRE(runCliCommand({"mesh-control-add", path(file)}).exitCode == cli::ExitCode::Success);

    const CliRun human = runCliCommand({"mesh-settings", path(file)});
    CHECK_THAT(human.out, ContainsSubstring("Global target size: BetterCAD default"));
    const CliRun json = runCliCommand({"mesh-settings", path(file), "--json"});
    CHECK_THAT(json.out, ContainsSubstring("\"globalTargetSize\": null"));
}

TEST_CASE("MeshingCli_SettingsNeedsAControl", "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "nocontrol");
    const CliRun run = runCliCommand({"mesh-settings", path(file)});
    CHECK(run.exitCode == cli::ExitCode::Failure);
    CHECK(run.out.empty());  // "could not answer" prints nothing to stdout
    CHECK_THAT(run.err, ContainsSubstring("no_mesh_control"));
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_GlobalSizeTakesEitherUnitSpelling", "[cli][meshing][meshcli]") {
    // 10mm and 0.01m are the same Length, through the CLI's own quantity
    // parser -- the one every other command uses. A mesh-only parser could
    // have disagreed with the rest of the tool.
    TempDir temp;
    for (const std::string_view spelling : {"10mm", "0.01m", "1cm"}) {
        const std::filesystem::path file =
            writeBlock(temp, std::format("unit-{}", spelling));
        REQUIRE(runCliCommand({"mesh-control-add", path(file)}).exitCode ==
                cli::ExitCode::Success);
        INFO(spelling);
        REQUIRE(runCliCommand({"mesh-set-global-size", path(file), std::string{spelling}})
                    .exitCode == cli::ExitCode::Success);
        const CliRun json = runCliCommand({"mesh-settings", path(file), "--json"});
        CHECK_THAT(json.out, ContainsSubstring("\"value\": 0.01"));
    }
}

TEST_CASE("MeshingCli_AnInvalidGlobalSizeFailsAndChangesNothing", "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "invalid");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "8mm"}).exitCode ==
            cli::ExitCode::Success);
    const std::string before = readFile(file);

    struct Case {
        std::string_view value;
        cli::ExitCode code;
    };
    // EXIT 1 WHEN THE CORE REFUSES THE VALUE, EXIT 2 WHEN THE COMMAND LINE IS
    // UNREADABLE, and the difference is the point: "-1mm" and "0mm" reach
    // P16-SIZE-001's validator and come back with its diagnostic, while
    // "nanmm" and "1zz" never become a quantity at all.
    for (const Case& test : std::array{Case{"-1mm", cli::ExitCode::Failure},
                                       Case{"0mm", cli::ExitCode::Failure},
                                       Case{"0", cli::ExitCode::Failure},
                                       Case{"nanmm", cli::ExitCode::UsageError},
                                       Case{"infmm", cli::ExitCode::UsageError},
                                       Case{"1zz", cli::ExitCode::UsageError}}) {
        INFO(test.value);
        const CliRun run =
            runCliCommand({"mesh-set-global-size", path(file), "--", std::string{test.value}});
        CHECK(run.exitCode == test.code);
        // THE DOCUMENT IS UNTOUCHED, byte for byte. runEdit saves only on
        // success, so this is a property of the edit spine -- measured rather
        // than assumed.
        CHECK(readFile(file) == before);
    }

    // And the core's own words came through for the two it refused.
    const CliRun negative = runCliCommand({"mesh-set-global-size", path(file), "--", "-1mm"});
    CHECK_THAT(negative.err, ContainsSubstring("not positive"));
}

TEST_CASE("MeshingCli_LocalSizingAddsAndRemovesByFace", "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "local");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "8mm"}).exitCode ==
            cli::ExitCode::Success);

    REQUIRE(runCliCommand({"mesh-local-add", path(file), "face:Solid:end_cap", "2mm"}).exitCode ==
            cli::ExitCode::Success);
    CHECK_THAT(runCliCommand({"mesh-settings", path(file)}).out,
               ContainsSubstring("face:Solid:end_cap  2 mm"));

    // ONE CONTROL PER FACE: P16-SIZE-001's rule, reached through the command.
    const CliRun twice =
        runCliCommand({"mesh-local-add", path(file), "face:Solid:end_cap", "1mm"});
    CHECK(twice.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(twice.err, ContainsSubstring("already exists"));

    // Removed BY FACE, which is the control's identity -- never by position.
    REQUIRE(runCliCommand({"mesh-local-remove", path(file), "face:Solid:end_cap"}).exitCode ==
            cli::ExitCode::Success);
    CHECK_THAT(runCliCommand({"mesh-settings", path(file)}).out,
               ContainsSubstring("Local sizing (0)"));

    // A face with no control is a mistake worth reporting, not a silent no-op.
    const CliRun missing = runCliCommand({"mesh-local-remove", path(file), "face:Solid:end_cap"});
    CHECK(missing.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(missing.err, ContainsSubstring("no local mesh sizing control"));
}

TEST_CASE("MeshingCli_AMalformedFaceReferenceIsAUsageError", "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "badface");
    REQUIRE(runCliCommand({"mesh-control-add", path(file)}).exitCode == cli::ExitCode::Success);
    for (const std::string_view bad : {"Solid:end_cap", "face:Solid", "face:Solid:sideways",
                                       "face:NoSuchFeature:end_cap", "node:4"}) {
        INFO(bad);
        const CliRun run = runCliCommand({"mesh-local-add", path(file), std::string{bad}, "2mm"});
        CHECK(run.exitCode != cli::ExitCode::Success);
        CHECK(run.out.empty());
    }
    // A NodeId is not a geometry reference, and the refusal says what a face
    // looks like instead.
    CHECK_THAT(runCliCommand({"mesh-local-add", path(file), "node:4", "2mm"}).err,
               ContainsSubstring("face:<feature>:<role>"));
}

// ---------------------------------------------------------------------------
// Generation and the queries
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_GenerateReportsWhatItBuiltAndSavesNoMesh", "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeTube(temp, "generate");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "5mm"}).exitCode ==
            cli::ExitCode::Success);
    const std::string before = readFile(file);

    const CliRun run = runCliCommand({"mesh-generate", path(file), "--json"});
    CHECK(run.exitCode == cli::ExitCode::Success);
    CHECK(std::stoul(field(run.out, "nodeCount")) > 0U);
    CHECK(std::stoul(field(run.out, "elementCount")) > 0U);
    CHECK_THAT(run.out, ContainsSubstring("\"Tet4\""));

    // THE MESH IS NOT PERSISTED. The document is byte-identical afterwards,
    // which is P16-PERSIST-001's authority boundary expressed headlessly.
    CHECK(readFile(file) == before);
}

TEST_CASE("MeshingCli_InfoSummarisesAndNeverListsElements", "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeTube(temp, "info");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "3mm"}).exitCode ==
            cli::ExitCode::Success);

    const CliRun run = runCliCommand({"mesh-info", path(file), "--json"});
    REQUIRE(run.exitCode == cli::ExitCode::Success);
    const std::size_t elements = std::stoul(field(run.out, "elementCount"));
    REQUIRE(elements > 100U);

    // A SUMMARY AT ANY SIZE. The output must not grow with the mesh: a report
    // that printed a handle per element would be useless to a person and
    // ruinous in a log. Checked by size, with a generous bound -- the point is
    // that it is bounded at all.
    CHECK(run.out.size() < 2000U);
    CHECK(run.out.find("\"nodes\": [") == std::string::npos);
    CHECK(run.out.find("\"elements\": [") == std::string::npos);
}

TEST_CASE("MeshingCli_ValidateGatesOnStructureAndQualityDoesNot", "[cli][meshing][meshcli]") {
    // The two are kept apart on purpose: a poor aspect ratio is not a
    // structural defect, and a command that conflated them would make a thin
    // element look like a broken one.
    TempDir temp;
    const std::filesystem::path file = writeTube(temp, "validate");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "4mm"}).exitCode ==
            cli::ExitCode::Success);

    const CliRun validate = runCliCommand({"mesh-validate", path(file), "--json"});
    CHECK(validate.exitCode == cli::ExitCode::Success);
    CHECK_THAT(validate.out, ContainsSubstring("\"dataValid\": true"));

    const CliRun quality = runCliCommand({"mesh-quality", path(file), "--json"});
    // A warning or a failure count is a report, not a failure of the command.
    CHECK(quality.exitCode == cli::ExitCode::Success);
    CHECK_THAT(quality.out, ContainsSubstring("\"invalidElements\": 0"));
    CHECK_THAT(quality.out, ContainsSubstring("\"structurallyValid\": true"));
}

TEST_CASE("MeshingCli_BoundariesReportsMappedFacetsAndUnresolvedIsAnAnswer",
          "[cli][meshing][meshcli]") {
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "boundaries");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "8mm"}).exitCode ==
            cli::ExitCode::Success);

    const CliRun cap =
        runCliCommand({"mesh-boundaries", path(file), "face:Solid:end_cap", "--json"});
    CHECK(cap.exitCode == cli::ExitCode::Success);
    CHECK_THAT(cap.out, ContainsSubstring("\"resolved\": true"));
    CHECK(std::stoul(field(cap.out, "facetCount")) > 0U);

    // A FACE THAT RESOLVES TO NOTHING IS STILL A SUCCESSFUL QUERY. Observation
    // is not operation: reporting "unresolved" is an answer, and only a
    // command that needed the region would fail.
    const CliRun absent =
        runCliCommand({"mesh-boundaries", path(file), "face:Solid:side:9999", "--json"});
    CHECK(absent.exitCode == cli::ExitCode::Success);
    CHECK_THAT(absent.out, ContainsSubstring("\"resolved\": false"));
    CHECK_THAT(absent.out, ContainsSubstring("\"facetCount\": 0"));
}

// ---------------------------------------------------------------------------
// Failure propagation
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_CoreFailuresPropagateWithTheirOwnDiagnostics",
          "[cli][meshing][meshcli][failure]") {
    TempDir temp;

    SECTION("a document with no body") {
        const std::filesystem::path file = writeEmpty(temp);
        const CliRun run = runCliCommand({"mesh-control-add", path(file)});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK_THAT(run.err, ContainsSubstring("no_body"));
    }
    SECTION("a document that does not exist") {
        const CliRun run = runCliCommand({"mesh-info", path(temp.path() / "nope.bcad")});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("document_not_loaded"));
    }
    SECTION("a malformed document") {
        const std::filesystem::path file = temp.path() / "broken.bcad";
        std::ofstream{file} << "{\"format\": ";
        const CliRun run = runCliCommand({"mesh-settings", path(file)});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK_THAT(run.err, ContainsSubstring("document_not_loaded"));
    }
    SECTION("an unresolvable local control makes generation refuse") {
        // The core refuses to mesh while ignoring a refinement the user asked
        // for (P16-PERSIST-001 finding A8), and the CLI must not work around
        // it. The reference is well formed and names no current face.
        const std::filesystem::path file = writeBlock(temp, "unresolved");
        REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "8mm"}).exitCode ==
                cli::ExitCode::Success);
        REQUIRE(runCliCommand({"mesh-local-add", path(file), "face:Solid:side:9999", "2mm"})
                    .exitCode == cli::ExitCode::Success);
        const CliRun run = runCliCommand({"mesh-generate", path(file)});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("mesh_generation_failed"));
        CHECK_THAT(run.err, ContainsSubstring("does not resolve"));
    }
}

// ---------------------------------------------------------------------------
// Core / CLI equivalence
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_EveryExposedFieldMatchesTheCore", "[cli][meshing][meshcli][equivalence]") {
    // THE HARD GATE, in its in-process form: the same document through the core
    // API and through the CLI, field by field. The separate-process form is the
    // cli.mesh.* process tests and the end-to-end script.
    TempDir temp;
    const std::filesystem::path file = writeTube(temp, "equivalence");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "4mm"}).exitCode ==
            cli::ExitCode::Success);
    REQUIRE(runCliCommand({"mesh-local-add", path(file), "face:Solid:end_cap", "1.5mm"})
                .exitCode == cli::ExitCode::Success);

    const CoreResult core = viaCore(file);
    const CliRun info = runCliCommand({"mesh-info", path(file), "--json"});
    REQUIRE(info.exitCode == cli::ExitCode::Success);
    const CliRun quality = runCliCommand({"mesh-quality", path(file), "--json"});
    REQUIRE(quality.exitCode == cli::ExitCode::Success);
    const CliRun validate = runCliCommand({"mesh-validate", path(file), "--json"});
    REQUIRE(validate.exitCode == cli::ExitCode::Success);

    CHECK(std::stoul(field(info.out, "nodeCount")) == core.nodes);
    CHECK(std::stoul(field(info.out, "elementCount")) == core.elements);
    CHECK(std::stoul(field(info.out, "boundaryTriangleCount")) == core.boundaryTriangles);
    // The volume as the core computed it. Exact: both read the same
    // VolumeMesh::tetrahedralVolume, so a difference would mean the CLI had
    // recomputed something.
    CHECK_THAT(std::stod(field(info.out, "value")), WithinRel(core.volumeSi, 1e-12));

    CHECK(std::stoul(field(quality.out, "invalidElements")) == core.invalid);
    CHECK(std::stoul(field(quality.out, "warningElements")) == core.warnings);
    CHECK(std::stoul(field(quality.out, "failureElements")) == core.failures);
    CHECK(field(validate.out, "dataValid") == (core.dataValid ? "true" : "false"));

    // The element count is large enough that an accidental agreement is not
    // plausible.
    REQUIRE(core.elements > 100U);
}

TEST_CASE("MeshingCli_HumanAndJsonAgree", "[cli][meshing][meshcli][equivalence]") {
    // Two renderings of one core result. A tool whose prose said PASS while
    // its payload said false would be worse than having only one of them.
    TempDir temp;
    const std::filesystem::path file = writeTube(temp, "agree");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "5mm"}).exitCode ==
            cli::ExitCode::Success);

    const CliRun humanInfo = runCliCommand({"mesh-info", path(file)});
    const CliRun jsonInfo = runCliCommand({"mesh-info", path(file), "--json"});
    REQUIRE(humanInfo.exitCode == cli::ExitCode::Success);
    REQUIRE(jsonInfo.exitCode == cli::ExitCode::Success);
    CHECK_THAT(humanInfo.out,
               ContainsSubstring(std::format("Nodes: {}", field(jsonInfo.out, "nodeCount"))));
    CHECK_THAT(humanInfo.out, ContainsSubstring(std::format(
                                  "Elements: {} Tet4", field(jsonInfo.out, "elementCount"))));

    const CliRun humanValidate = runCliCommand({"mesh-validate", path(file)});
    const CliRun jsonValidate = runCliCommand({"mesh-validate", path(file), "--json"});
    CHECK(humanValidate.exitCode == jsonValidate.exitCode);
    const bool valid = field(jsonValidate.out, "dataValid") == "true";
    CHECK_THAT(humanValidate.out, ContainsSubstring(valid ? "PASS" : "FAIL"));
}

TEST_CASE("MeshingCli_OutputIsDeterministic", "[cli][meshing][meshcli][equivalence]") {
    // Repeated queries must produce identical bytes: the local controls, the
    // boundary sets, the metrics and the findings all come from ordered
    // containers, and nothing here iterates an unordered one.
    TempDir temp;
    const std::filesystem::path file = writeBlock(temp, "determinism");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "8mm"}).exitCode ==
            cli::ExitCode::Success);
    // Added in an order that is not the canonical one.
    REQUIRE(runCliCommand({"mesh-local-add", path(file), "face:Solid:start_cap", "3mm"})
                .exitCode == cli::ExitCode::Success);
    REQUIRE(runCliCommand({"mesh-local-add", path(file), "face:Solid:end_cap", "2mm"}).exitCode ==
            cli::ExitCode::Success);

    for (const std::string_view command : {"mesh-settings", "mesh-info", "mesh-quality",
                                           "mesh-validate", "mesh-boundaries"}) {
        INFO(command);
        const CliRun first = runCliCommand({std::string{command}, path(file), "--json"});
        const CliRun second = runCliCommand({std::string{command}, path(file), "--json"});
        REQUIRE(first.exitCode == second.exitCode);
        CHECK(first.out == second.out);
    }
}

// ---------------------------------------------------------------------------
// Save / load / remesh
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_SettingsSurviveASaveAndLoadAndRemeshTheSame",
          "[cli][meshing][meshcli][roundtrip]") {
    // The headless expression of P16-PERSIST-001's hard gate: the intent is
    // written, read back and produces the same mesh -- and nothing about the
    // mesh itself was stored to make that work.
    TempDir temp;
    const std::filesystem::path file = writeTube(temp, "roundtrip");
    REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "5mm"}).exitCode ==
            cli::ExitCode::Success);
    REQUIRE(runCliCommand({"mesh-local-add", path(file), "face:Solid:end_cap", "1.5mm"})
                .exitCode == cli::ExitCode::Success);

    const CliRun before = runCliCommand({"mesh-settings", path(file), "--json"});
    const CliRun meshBefore = runCliCommand({"mesh-info", path(file), "--json"});
    REQUIRE(before.exitCode == cli::ExitCode::Success);
    REQUIRE(meshBefore.exitCode == cli::ExitCode::Success);

    // Copy the file, which is what a second process would open.
    const std::filesystem::path copy = temp.path() / "reopened.bcad";
    std::filesystem::copy_file(file, copy);
    const CliRun after = runCliCommand({"mesh-settings", cliPath(copy), "--json"});
    const CliRun meshAfter = runCliCommand({"mesh-info", cliPath(copy), "--json"});
    REQUIRE(after.exitCode == cli::ExitCode::Success);

    CHECK(after.out == before.out);
    // Semantically the same mesh: the counts and the volume, not the handles.
    CHECK(field(meshAfter.out, "nodeCount") == field(meshBefore.out, "nodeCount"));
    CHECK(field(meshAfter.out, "elementCount") == field(meshBefore.out, "elementCount"));
    CHECK(field(meshAfter.out, "value") == field(meshBefore.out, "value"));
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_HelpDocumentsTheMeshCommands", "[cli][meshing][meshcli]") {
    const CliRun help = runCliCommand({"help"});
    REQUIRE(help.exitCode == cli::ExitCode::Success);
    for (const std::string_view command :
         {"mesh-settings", "mesh-generate", "mesh-info", "mesh-quality", "mesh-validate",
          "mesh-boundaries", "mesh-control-add", "mesh-set-global-size", "mesh-local-add",
          "mesh-local-remove"}) {
        INFO(command);
        CHECK_THAT(help.out, ContainsSubstring(std::string{command}));
    }
    // The reference syntax and the unit convention are documented where a
    // reader needs them, not only in a commit message.
    CHECK_THAT(help.out, ContainsSubstring("face:<feature>:<role>"));
    CHECK_THAT(help.out, ContainsSubstring("millimetres"));

    const CliRun unknown = runCliCommand({"mesh-frobnicate"});
    CHECK(unknown.exitCode == cli::ExitCode::UsageError);
}

// ---------------------------------------------------------------------------
// The two gaps mutation testing found
// ---------------------------------------------------------------------------

TEST_CASE("MeshingCli_ADocumentThatWillNotRegenerateIsNotMeshed",
          "[cli][meshing][meshcli][failure]") {
    // FOUND BY MUTATION M6, which removed the regeneration check from the CLI
    // and survived: every fixture here regenerated cleanly, so ignoring the
    // result was indistinguishable from honouring it.
    //
    // The brief's §47 is explicit -- "do not reuse stale geometry because the
    // command is headless" -- and that is exactly the behaviour a missing
    // check would break. So this fixture FAILS to regenerate: the extrude's
    // profile is a single line, which encloses nothing, so there is no face to
    // sweep.
    TempDir temp;
    Document document{"Broken"};
    auto sketch = std::make_unique<sketch::Sketch>("Open", Frame3D::xy());
    const EntityId a = require(sketch->addPoint(Point2D{0_mm, 0_mm}));
    const EntityId b = require(sketch->addPoint(Point2D{30_mm, 0_mm}));
    (void)require(sketch->addLine(a, b));
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    (void)require(document.addObject(std::move(*extrude)));

    const std::filesystem::path file = temp.path() / "broken.bcad";
    REQUIRE(io::saveDocument(document, file).has_value());

    // The premise: this really does fail to regenerate. Asserted directly, so
    // the test cannot silently become vacuous if the kernel one day closes a
    // single line into a face.
    {
        Result<Document> reloaded = io::loadDocument(file);
        REQUIRE(reloaded.has_value());
        features::Regenerator regenerator;
        const Result<features::RegenerationReport> report = regenerator.regenerate(*reloaded);
        // regenerate() SUCCEEDS and records the failure in its report -- it
        // returns an error only for a document-level fault. That distinction
        // is the production defect M6 uncovered: the CLI was checking only
        // the Result and ignoring report->failed.
        REQUIRE(report.has_value());
        INFO("failed: " << report->failed.size() << ", blocked: " << report->blocked.size());
        REQUIRE_FALSE(report->failed.empty());
    }

    // Every report must refuse, with the core's own reason, and print nothing
    // to stdout. A CLI that skipped the check would reach the mesher with no
    // body and report something else -- or worse, mesh an older one.
    for (const std::string_view command : {"mesh-settings", "mesh-generate", "mesh-info",
                                           "mesh-quality", "mesh-validate", "mesh-boundaries"}) {
        INFO(command);
        const CliRun run = runCliCommand({std::string{command}, path(file)});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("regeneration_failed"));
    }

    // A MUTATING VERB STILL SUCCEEDS, and that is correct rather than a gap.
    //
    // Creating meshing intent does not require working geometry: the control
    // names a feature, the mesh is derived, and a user whose model is
    // temporarily broken may legitimately set up meshing before fixing it.
    // This is the same principle P16-PERSIST-001 settled for an unresolved
    // face reference -- intent is kept, resolution is derived.
    //
    // Only MESHING needs a body, and the six reports above are what refuse.
    // Making the edit regenerate would change the edit spine's contract for
    // every verb, to no engineering benefit.
    const CliRun add = runCliCommand({"mesh-control-add", path(file)});
    CHECK(add.exitCode == cli::ExitCode::Success);
    // ...and the report that needs geometry still refuses afterwards, for the
    // model's reason and not for a missing control.
    const CliRun generate = runCliCommand({"mesh-generate", path(file)});
    CHECK(generate.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(generate.err, ContainsSubstring("regeneration_failed"));
    CHECK_THAT(generate.err, ContainsSubstring("did not regenerate"));
}

TEST_CASE("MeshingCli_TheListingIsCanonicalAndNotInsertionOrder",
          "[cli][meshing][meshcli][equivalence]") {
    // FOUND BY MUTATION M7, which listed the stored vector instead of the
    // ordered accessor and survived. The determinism test compares two RUNS of
    // one document, and stored order is perfectly stable across runs -- so it
    // proved determinism and said nothing about canonicality.
    //
    // This builds the SAME canonical intent twice, adding the two local
    // controls in OPPOSITE orders, and requires identical output. P16-PERSIST
    // -001 tested its serializer this way; the lesson did not carry over to
    // the CLI until a mutation pointed at it.
    TempDir temp;
    const auto settingsOf = [&temp](bool reversed) {
        const std::filesystem::path file =
            writeBlock(temp, reversed ? "order-b" : "order-a");
        REQUIRE(runCliCommand({"mesh-control-add", path(file), "--size", "8mm"}).exitCode ==
                cli::ExitCode::Success);
        // start_cap sorts before end_cap (FaceRole's own order), so adding
        // them the other way round makes the stored order differ from the
        // canonical one.
        const std::array<std::string, 2> faces{"face:Solid:start_cap", "face:Solid:end_cap"};
        const std::array<std::string, 2> sizes{"3mm", "2mm"};
        for (std::size_t i = 0; i < 2U; ++i) {
            const std::size_t at = reversed ? 1U - i : i;
            REQUIRE(runCliCommand({"mesh-local-add", path(file), faces[at], sizes[at]}).exitCode ==
                    cli::ExitCode::Success);
        }
        const CliRun run = runCliCommand({"mesh-settings", path(file), "--json"});
        REQUIRE(run.exitCode == cli::ExitCode::Success);
        return run.out;
    };

    const std::string forward = settingsOf(false);
    const std::string reversed = settingsOf(true);
    // The two documents differ only in the order the controls were added, and
    // that order carries no meaning (P16-SIZE-001), so the listing must not
    // expose it.
    CHECK(forward == reversed);
    // And the canonical order really is start_cap first, so the test is
    // comparing something.
    CHECK(forward.find("start_cap") < forward.find("end_cap"));
}
