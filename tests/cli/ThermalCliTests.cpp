// P18-CLI-001: headless thermal analysis.
//
// WHAT THESE TESTS ARE FOR. The commands are adapters over the qualified chain, so the thing worth proving is not that they print
// something but that they print the SAME thing the core would say, that they fail exactly where the core fails, that a failure
// leaves the file as it was, and that the exit status carries the gate. A test that only checked "exit 0 and some words" would pass
// against a CLI that had quietly grown its own conduction formula, its own residual threshold or its own idea of a default material.
//
// SO EVERY ENGINEERING ASSERTION HERE IS AN EQUIVALENCE. The core is run in process on the SAME FILE the CLI read, its result is
// read through its own accessors, and the number the CLI printed is compared against it bit for bit (the writer prints the shortest
// string that reads back exactly, so equality is the right comparison). Nothing is compared against a literal that a change to the
// physics could not move, except the handful of closed forms that are physics rather than plumbing.
//
// EVERY PAYLOAD IS PARSED, not searched: `MiniJson.hpp` is a strict RFC 8259 reader that is independent of the CLI's writer, so a
// stray `nan`, a trailing banner, a repeated key or a misplaced field fails the parse before any value is looked at.
//
// For the stronger form -- the real executable in separate processes -- see the process tests registered in tests/CMakeLists.txt
// (cli.thermal.*) and the multi-process script in ThermalWorkflow.cmake.

#include "CliRunner.hpp"
#include "Commands.hpp"
#include "MiniJson.hpp"
#include "ThermalOutput.hpp"
#include "renderer/ThermalVizTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/thermal/ThermalAnalysisRun.hpp>
#include <bettercad/thermal/ThermalCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using thermal::ThermalAnalysisMode;
using thermal::ThermalBoundaryCondition;
using thermal::ThermalLoad;
using thermal::ValidationCode;
using vztest::VizStudy;

namespace {

constexpr cli::ExitCode kOk = cli::ExitCode::Success;
constexpr cli::ExitCode kFailed = cli::ExitCode::Failure;
constexpr cli::ExitCode kUsage = cli::ExitCode::UsageError;

[[nodiscard]] std::uint64_t bitsOf(double value) { return std::bit_cast<std::uint64_t>(value); }

[[nodiscard]] std::filesystem::path saveStudy(const VizStudy& study, const TempDir& temp, std::string_view name) {
    const std::filesystem::path file = temp.path() / std::format("{}.bcad", name);
    const Result<void> saved = io::saveDocument(study.document, file);
    INFO((saved ? std::string{} : saved.error().message));
    REQUIRE(saved.has_value());
    return file;
}

[[nodiscard]] std::filesystem::path exampleFile(std::string_view name) {
    return std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / std::string{name};
}

[[nodiscard]] std::filesystem::path copyOf(const std::filesystem::path& source, const TempDir& temp, std::string_view name) {
    const std::filesystem::path target = temp.path() / std::format("{}.bcad", name);
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
    return target;
}

[[nodiscard]] std::string bytesOf(const std::filesystem::path& file) { return readFile(file); }

/// The text of a face, the way the command line writes one (the feature of VizStudy is named "Solid").
[[nodiscard]] std::string faceText(std::string_view role) { return std::format("face:Solid:{}", role); }
[[nodiscard]] std::string sideText(const VizStudy& study, std::size_t which) {
    return std::format("face:Solid:side:{}", study.lines.at(which).value());
}

[[nodiscard]] CliRun invoke(const std::vector<std::string>& args) { return runCliCommand(args); }

/// A command that could not answer: exit 1, NOTHING on stdout, the stable code on stderr.
void requireRefused(const CliRun& run, std::string_view code) {
    INFO("stdout:\n" << run.out << "\nstderr:\n" << run.err);
    CHECK(run.exitCode == kFailed);
    CHECK(run.out.empty());
    CHECK_THAT(run.err, ContainsSubstring(std::format(": {}: ", code)));
}

/// A command line the CLI could not read: exit 2, nothing on stdout, a usage line on stderr.
void requireUsage(const CliRun& run) {
    INFO("stdout:\n" << run.out << "\nstderr:\n" << run.err);
    CHECK(run.exitCode == kUsage);
    CHECK(run.out.empty());
    CHECK_THAT(run.err, ContainsSubstring("Usage:"));
}

/// The core's own answer on the file the CLI will read: load, regenerate, mesh, run. The oracle every equivalence test compares to.
struct CoreAnswer {
    Document document;
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ThermalAnalysisId analysis{};
    std::optional<thermal::ThermalAnalysisRun> run;

    explicit CoreAnswer(Document loaded) : document(std::move(loaded)) {}
};

[[nodiscard]] std::unique_ptr<CoreAnswer> coreAnswer(const std::filesystem::path& file) {
    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    auto answer = std::make_unique<CoreAnswer>(std::move(*loaded));
    const Result<features::RegenerationReport> report = answer->regenerator.regenerate(answer->document);
    REQUIRE(report.has_value());
    REQUIRE(report->failed.empty());
    const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(answer->document);
    REQUIRE(ids.size() == 1U);
    answer->analysis = ids.front();
    const thermal::ThermalAnalysis* analysis = thermal::findThermalAnalysis(answer->document, answer->analysis);
    REQUIRE(analysis != nullptr);
    const Result<const meshing::VolumeMesh*> mesh =
        answer->mesher.generate(answer->document, answer->regenerator, analysis->definition().mesh);
    INFO((mesh ? std::string{} : mesh.error().message));
    REQUIRE(mesh.has_value());
    answer->run = thermal::runThermalAnalysis(answer->document, answer->regenerator, answer->mesher, answer->analysis);
    return answer;
}

/// The extremum of the core's final temperatures with the ascending-NodeId tie-break the CLI states, found by a separate loop.
struct CoreRange {
    double low = 0.0;
    std::uint64_t lowNode = 0;
    double high = 0.0;
    std::uint64_t highNode = 0;
};
[[nodiscard]] CoreRange coreTemperatureRange(const thermal::ThermalAnalysisRun& run) {
    REQUIRE(run.post.has_value());
    REQUIRE(run.dofMap.has_value());
    const auto& temperatures = run.post->finalState().temperatures;
    REQUIRE_FALSE(temperatures.empty());
    CoreRange range;
    bool first = true;
    for (std::size_t i = 0; i < temperatures.size(); ++i) {
        const Result<meshing::NodeId> node = run.dofMap->nodeOf(thermal::ThermalDofIndex::fromValue(i));
        REQUIRE(node.has_value());
        const double t = temperatures[i].si();
        const std::uint64_t id = node->value();
        if (first) {
            range = CoreRange{t, id, t, id};
            first = false;
            continue;
        }
        if (t < range.low || (t == range.low && id < range.lowNode)) {
            range.low = t;
            range.lowNode = id;
        }
        if (t > range.high || (t == range.high && id < range.highNode)) {
            range.high = t;
            range.highNode = id;
        }
    }
    return range;
}

/// A steady analysis with generation, a held cap and a convecting cap: several free unknowns, so the field varies.
void steadyStudy(VizStudy& study) { vztest::multiFree(study); }

void transientStudy(VizStudy& study, double dt = 0.5, double end = 5.0,
                    thermal::TransientOutputPolicy output = thermal::TransientOutputPolicy::EveryStep) {
    vztest::define(study, vztest::transientDefinition(
                              study, {vztest::generationLoad(1, 2.0e5)},
                              {vztest::temperatureBc(1, study.startCap(), 300.0),
                               vztest::convectionBc(2, study.endCap(), 25.0, vztest::kAmbient)},
                              293.15, dt, end, output));
}

} // namespace

// ===========================================================================
// The test reader is itself checked
// ===========================================================================

TEST_CASE("ThermalCli_TheTestParserIsStrict", "[cli][thermal][stdout]") {
    // The gate is only as strong as the reader: a lenient one would pass a payload no real parser accepts.
    for (const std::string_view bad :
         {"{\"a\": nan}", "{\"a\": inf}", "{\"a\": Infinity}", "{\"a\": 1} trailing", "{\"a\": 1}{\"b\": 2}", "{\"a\": 1,}",
          "{\"a\": 01}", "{\"a\": 1, \"a\": 2}", "{'a': 1}", "{\"a\": \"x\ny\"}", "{\"a\": [1, 2}", "Loading...\n{\"a\": 1}",
          "{\"a\": 1}\nDone", "", "{\"a\": }", "{\"a\": 1e}", "{\"a\": -}"}) {
        std::string why;
        INFO("input: " << bad);
        CHECK_FALSE(JsonReader::parse(bad, &why).has_value());
    }
    std::string why;
    const std::optional<Json> good = JsonReader::parse("  {\"a\": [1, 2.5e-3, \"x\", null, true], \"b\": {\"c\": -0.5}}\n", &why);
    REQUIRE(good.has_value());
    CHECK((*good)["a"].size() == 5U);
    CHECK((*good)["b"]["c"].number() == -0.5);
}

// ===========================================================================
// Inspect
// ===========================================================================

TEST_CASE("ThermalCli_InspectAnalysis", "[cli][thermal][inspect]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "inspect");
    const CliRun run = invoke({"thermal-settings", cliPath(file)});
    REQUIRE(run.exitCode == kOk);
    CHECK(run.err.empty());
    CHECK_THAT(run.out, ContainsSubstring("Thermal analysis 'Study'"));
    CHECK_THAT(run.out, ContainsSubstring("mode:         steady"));
    CHECK_THAT(run.out, ContainsSubstring("1 volumetric_heat_source, 100000 W/m^3"));
    CHECK_THAT(run.out, ContainsSubstring("prescribed_temperature on face:Solid:start_cap, 300 K"));
    CHECK_THAT(run.out, ContainsSubstring("convection on face:Solid:end_cap, 25 W/(m^2 K) to 293.15 K"));
}

TEST_CASE("ThermalCli_InspectAnalysisStructured", "[cli][thermal][inspect][structured]") {
    VizStudy study;
    steadyStudy(study);
    vztest::define(study, [&] {
        auto d = study.definition();
        d.loads.push_back(vztest::surfaceLoad(5, study.side(1), -1234.5));
        return d;
    }());
    const TempDir temp;
    const auto file = saveStudy(study, temp, "inspect-json");
    const CliRun run = invoke({"thermal-settings", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    const Json j = parseObject(run.out);

    CHECK(j["analysisId"].number() == static_cast<double>(study.analysis.value()));
    CHECK(j["mode"].text() == "steady");
    CHECK(j["meshControl"].number() == static_cast<double>(study.control.value()));
    CHECK_FALSE(j["resultStored"].boolean());
    CHECK(j["initialTemperature"].isNull());
    CHECK(j["transient"].isNull());

    // Every canonical record, in the document's order, with its id, kind, reference and signed SI value.
    const thermal::ThermalAnalysisDefinition d = study.definition();
    REQUIRE(j["loads"].size() == d.loads.size());
    CHECK(j["loads"][0]["loadId"].number() == 1.0);
    CHECK(j["loads"][0]["type"].text() == "volumetric_heat_source");
    CHECK(bitsOf(j["loads"][0]["generation"].quantity("W/m^3")) == bitsOf(1.0e5));
    CHECK_FALSE(j["loads"][0].has("face"));
    CHECK(j["loads"][1]["loadId"].number() == 5.0);
    CHECK(j["loads"][1]["type"].text() == "surface_heat_flux");
    CHECK(j["loads"][1]["face"].text() == sideText(study, 1));
    CHECK(j["loads"][1]["faceResolves"].boolean());
    CHECK(bitsOf(j["loads"][1]["flux"].quantity("W/m^2")) == bitsOf(-1234.5));
    REQUIRE(j["boundaryConditions"].size() == 2U);
    CHECK(j["boundaryConditions"][0]["boundaryConditionId"].number() == 1.0);
    CHECK(j["boundaryConditions"][0]["type"].text() == "prescribed_temperature");
    CHECK(j["boundaryConditions"][0]["face"].text() == faceText("start_cap"));
    CHECK(bitsOf(j["boundaryConditions"][0]["temperature"].quantity("K")) == bitsOf(300.0));
    CHECK(j["boundaryConditions"][1]["type"].text() == "convection");
    CHECK(bitsOf(j["boundaryConditions"][1]["coefficient"].quantity("W/(m^2 K)")) == bitsOf(25.0));
    CHECK(bitsOf(j["boundaryConditions"][1]["ambient"].quantity("K")) == bitsOf(vztest::kAmbient));
    CHECK(bitsOf(j["solver"]["relativeResidualTolerance"].number()) == bitsOf(d.solver.relativeResidualTolerance));
    CHECK(bitsOf(j["solver"]["pivotFloor"].number()) == bitsOf(d.solver.pivotFloor));

    // NO MESH IDENTITY IN CANONICAL INTENT: the payload names no node, element, facet or degree of freedom.
    for (const char* forbidden : {"node", "Node", "facet", "Facet", "element", "Element", "dof", "Dof"}) {
        INFO("forbidden word: " << forbidden);
        CHECK(run.out.find(forbidden) == std::string::npos);
    }
}

TEST_CASE("ThermalCli_InspectTransientStructured", "[cli][thermal][inspect][structured][transient]") {
    VizStudy study;
    transientStudy(study, 0.25, 3.0);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "inspect-transient");
    const Json j = parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out);
    CHECK(j["mode"].text() == "transient");
    CHECK(bitsOf(j["initialTemperature"].quantity("K")) == bitsOf(293.15));
    CHECK(bitsOf(j["transient"]["timeStep"].quantity("s")) == bitsOf(0.25));
    CHECK(bitsOf(j["transient"]["endTime"].quantity("s")) == bitsOf(3.0));
    CHECK(j["transient"]["output"].text() == "every_step");
}

TEST_CASE("ThermalCli_InspectDoesNotMutateDocument", "[cli][thermal][inspect][bytes]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "readonly");
    const std::string before = bytesOf(file);
    for (const char* command : {"thermal-settings", "thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient",
                                "thermal-heat-flux", "thermal-balance"}) {
        for (const bool json : {false, true}) {
            INFO(command << (json ? " --json" : ""));
            std::vector<std::string> args{command, cliPath(file)};
            if (json) {
                args.emplace_back("--json");
            }
            const CliRun run = invoke(args);
            CHECK(run.exitCode == kOk);
            CHECK(bytesOf(file) == before);
        }
    }
}

TEST_CASE("ThermalCli_InspectShowsAnUnresolvedReferenceAndInventsNoSubstitute", "[cli][thermal][inspect][reference]") {
    VizStudy study;
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}};
    vztest::define(study, vztest::steadyDefinition(study, {vztest::surfaceLoad(3, nowhere, 700.0)},
                                                   {vztest::temperatureBc(1, study.startCap(), 300.0)}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "unresolved");
    const Json j = parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out);
    REQUIRE(j["loads"].size() == 1U);
    // The reference is exactly what was written, reported as not resolving. Inspecting is a successful answer.
    CHECK(j["loads"][0]["face"].text() == "face:Solid:side:9991");
    CHECK_FALSE(j["loads"][0]["faceResolves"].boolean());
}

TEST_CASE("ThermalCli_InspectIsDeterministic", "[cli][thermal][inspect][determinism]") {
    VizStudy study;
    transientStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "determinism");
    const std::string first = invoke({"thermal-settings", cliPath(file), "--json"}).out;
    for (int i = 0; i < 4; ++i) {
        CHECK(invoke({"thermal-settings", cliPath(file), "--json"}).out == first);
    }
}

TEST_CASE("ThermalCli_InspectWithoutAnAnalysisIsRefusedByCode", "[cli][thermal][inspect]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "bare");
    for (const char* command : {"thermal-settings", "thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient",
                                "thermal-heat-flux", "thermal-balance"}) {
        INFO(command);
        requireRefused(invoke({command, cliPath(file), "--json"}), "no_thermal_analysis");
    }
}

TEST_CASE("ThermalCli_TwoAnalysesAreAmbiguousAndNeverAFirstMatch", "[cli][thermal][inspect]") {
    VizStudy study;
    steadyStudy(study);
    auto second = thermal::ThermalAnalysis::create("Study2", thermal::ThermalAnalysisDefinition{.mesh = study.control});
    REQUIRE(second.has_value());
    REQUIRE(study.document.addObject(std::move(*second)).has_value());
    const TempDir temp;
    const auto file = saveStudy(study, temp, "ambiguous");
    for (const char* command : {"thermal-settings", "thermal-solve", "thermal-balance"}) {
        requireRefused(invoke({command, cliPath(file)}), "ambiguous_thermal_analysis");
    }
    const std::string before = bytesOf(file);
    requireRefused(invoke({"thermal-load-add", cliPath(file), "9", "--generation", "5"}), "ambiguous_thermal_analysis");
    CHECK(bytesOf(file) == before);
}

// ===========================================================================
// Analysis creation
// ===========================================================================

TEST_CASE("ThermalCli_AddSteadyAnalysis", "[cli][thermal][edit][analysis]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "add-steady");
    const CliRun run = invoke({"thermal-analysis-add", cliPath(file)});
    REQUIRE(run.exitCode == kOk);
    CHECK_THAT(run.out, ContainsSubstring("Added thermal analysis 'Thermal1'"));
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto ids = thermal::thermalAnalysisIds(*loaded);
    REQUIRE(ids.size() == 1U);
    const thermal::ThermalAnalysisDefinition d = thermal::findThermalAnalysis(*loaded, ids.front())->definition();
    CHECK(d.mode == ThermalAnalysisMode::Steady);
    CHECK_FALSE(d.initialCondition.has_value());
    CHECK_FALSE(d.transient.has_value());
    // THE SOLVER SETTINGS ARE THE DATA MODEL'S DEFAULTS: the CLI offers no tolerance of its own.
    CHECK(d.solver == thermal::ThermalSolverSettings{});
    CHECK(d.loads.empty());
    CHECK(d.boundaryConditions.empty());
}

TEST_CASE("ThermalCli_AddTransientAnalysis", "[cli][thermal][edit][analysis][transient]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "add-transient");
    const CliRun run = invoke({"thermal-analysis-add", cliPath(file), "--name", "Heating", "--mode", "transient",
                            "--initial-temperature", "295.5", "--time-step", "0.25", "--end-time", "3.5", "--output",
                            "every-step"});
    REQUIRE(run.exitCode == kOk);
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto ids = thermal::thermalAnalysisIds(*loaded);
    REQUIRE(ids.size() == 1U);
    const thermal::ThermalAnalysis* analysis = thermal::findThermalAnalysis(*loaded, ids.front());
    CHECK(analysis->name() == "Heating");
    const thermal::ThermalAnalysisDefinition& d = analysis->definition();
    CHECK(d.mode == ThermalAnalysisMode::Transient);
    REQUIRE(d.initialCondition.has_value());
    CHECK(bitsOf(d.initialCondition->value.si()) == bitsOf(295.5));
    REQUIRE(d.transient.has_value());
    CHECK(bitsOf(d.transient->timeStep.si()) == bitsOf(0.25));
    CHECK(bitsOf(d.transient->endTime.si()) == bitsOf(3.5));
    CHECK(d.transient->output == thermal::TransientOutputPolicy::EveryStep);
}

TEST_CASE("ThermalCli_AnalysisModeCompletenessIsTheCoresAndNeverGuessed", "[cli][thermal][edit][analysis][atomic]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "completeness");
    const std::string before = bytesOf(file);
    // A transient analysis with no initial temperature, no time step, no end time: nothing is invented to complete it.
    for (const std::vector<std::string>& extra : std::vector<std::vector<std::string>>{
             {"--mode", "transient"},
             {"--mode", "transient", "--time-step", "0.5", "--end-time", "2", "--output", "every-step"},
             {"--mode", "transient", "--initial-temperature", "300"},
             {"--mode", "transient", "--initial-temperature", "300", "--time-step", "0.5", "--output", "every-step"},
             {"--mode", "transient", "--initial-temperature", "300", "--time-step", "5", "--end-time", "1", "--output", "every-step"},
             // A zero, a negative or a below-absolute-zero value is refused by the core and is neither clamped nor made positive.
             {"--mode", "transient", "--initial-temperature", "300", "--time-step", "0", "--end-time", "2", "--output", "every-step"},
             {"--mode", "transient", "--initial-temperature", "300", "--time-step", "-0.5", "--end-time", "2", "--output", "every-step"},
             {"--mode", "transient", "--initial-temperature", "300", "--time-step", "0.5", "--end-time", "-2", "--output", "every-step"},
             {"--mode", "transient", "--initial-temperature", "-5", "--time-step", "0.5", "--end-time", "2", "--output", "every-step"},
             // A steady analysis given transient fields.
             {"--initial-temperature", "300"},
             {"--time-step", "0.5", "--end-time", "2", "--output", "every-step"},
             {"--output", "every-step"}}) {
        std::vector<std::string> args{"thermal-analysis-add", cliPath(file)};
        args.insert(args.end(), extra.begin(), extra.end());
        const CliRun run = invoke(args);
        INFO(run.err);
        CHECK(run.exitCode == kFailed);
        CHECK(run.out.empty());
        CHECK(bytesOf(file) == before);
    }
}

TEST_CASE("ThermalCli_AnalysisAddUsageErrors", "[cli][thermal][edit][analysis][usage]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "analysis-usage");
    const std::string before = bytesOf(file);
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--mode", "dynamic"}));
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--mode", "transient", "--initial-temperature", "abc"}));
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--mode", "transient", "--initial-temperature", "nan"}));
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--mode", "transient", "--initial-temperature", "20degC"}));
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--output", "sometimes"}));
    // TIME SETTINGS NEED --output, AND THERE IS NO DEFAULT FOR IT: the data model's own default keeps only the last state, which a
    // multi-step analysis cannot be accepted with, so choosing it silently would build an analysis that can never run.
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--mode", "transient", "--initial-temperature", "300", "--time-step",
                         "0.5", "--end-time", "2"}));
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--time-step", "0.5"}));
    requireUsage(invoke({"thermal-analysis-add", cliPath(file), "extra"}));
    requireUsage(invoke({"thermal-analysis-add"}));
    CHECK(bytesOf(file) == before);
}

TEST_CASE("ThermalCli_ASecondAnalysisIsRefused", "[cli][thermal][edit][analysis]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "second");
    const std::string before = bytesOf(file);
    requireRefused(invoke({"thermal-analysis-add", cliPath(file)}), "thermal_analysis_exists");
    CHECK(bytesOf(file) == before);
}

// ===========================================================================
// Loads
// ===========================================================================

TEST_CASE("ThermalCli_AddSurfaceHeatFlux", "[cli][thermal][edit][load]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "add-flux");
    const CliRun run = invoke({"thermal-load-add", cliPath(file), "4", "--flux", "750.5", "--face", sideText(study, 2)});
    REQUIRE(run.exitCode == kOk);
    CHECK_THAT(run.out, ContainsSubstring("Added load 4: surface heat flux 750.5 W/m^2 into the body on"));

    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const thermal::ThermalAnalysisDefinition d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    REQUIRE(d.loads.size() == 2U);
    const ThermalLoad& added = d.loads.back();
    CHECK(added.id().value() == 4U);
    REQUIRE(added.surfaceHeatFlux() != nullptr);
    CHECK(bitsOf(added.surfaceHeatFlux()->flux.si()) == bitsOf(750.5));
    // THE CANONICAL REFERENCE, NOT A MESH FACET: exactly the FaceName the grammar named.
    CHECK(added.surfaceHeatFlux()->face == study.side(2));
    CHECK(d.loads.front() == study.definition().loads.front());   // the earlier load is untouched
}

TEST_CASE("ThermalCli_AddVolumetricGeneration", "[cli][thermal][edit][load]") {
    VizStudy study;
    transientStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "add-generation");
    const CliRun run = invoke({"thermal-load-add", cliPath(file), "6", "--generation", "-2.5e4W/m^3"});
    REQUIRE(run.exitCode == kOk);
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const thermal::ThermalAnalysisDefinition d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    REQUIRE(d.loads.size() == 2U);
    REQUIRE(d.loads.back().volumetricHeatSource() != nullptr);
    // A NEGATIVE GENERATION IS A SINK AND IS KEPT AS GIVEN: no abs(), no clamp.
    CHECK(bitsOf(d.loads.back().volumetricHeatSource()->generation.si()) == bitsOf(-2.5e4));
}

TEST_CASE("ThermalCli_NegativeFluxIsASinkAndIsNotCorrected", "[cli][thermal][edit][load][sign]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "sink");
    REQUIRE(invoke({"thermal-load-add", cliPath(file), "4", "--flux", "-1234.5", "--face", faceText("end_cap")}).exitCode == kOk);
    const Json j = parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out);
    CHECK(bitsOf(j["loads"][1]["flux"].quantity("W/m^2")) == bitsOf(-1234.5));
    // And the core agrees about what a sink does: the surface term of the balance is negative.
    const auto answer = coreAnswer(file);
    REQUIRE(answer->run->accepted());
    CHECK(answer->run->balance->finalState().surfaceLoads.front().heatInto.si() < 0.0);
}

TEST_CASE("ThermalCli_RemoveLoad", "[cli][thermal][edit][load]") {
    VizStudy study;
    steadyStudy(study);
    vztest::define(study, [&] {
        auto d = study.definition();
        d.loads.push_back(vztest::surfaceLoad(7, study.side(0), 100.0));
        d.loads.push_back(vztest::surfaceLoad(3, study.side(1), 200.0));
        return d;
    }());
    const TempDir temp;
    const auto file = saveStudy(study, temp, "remove-load");
    const CliRun run = invoke({"thermal-load-remove", cliPath(file), "7"});
    REQUIRE(run.exitCode == kOk);
    CHECK_THAT(run.out, ContainsSubstring("Removed load 7"));
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const thermal::ThermalAnalysisDefinition d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    // BY STABLE ID, not by position: id 7 was in the middle, and the others keep their ids and their order.
    REQUIRE(d.loads.size() == 2U);
    CHECK(d.loads[0].id().value() == 1U);
    CHECK(d.loads[1].id().value() == 3U);
}

TEST_CASE("ThermalCli_LoadIdsAreTheUsersAndNeverAContainerIndex", "[cli][thermal][edit][load][id]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "ids");
    REQUIRE(invoke({"thermal-load-add", cliPath(file), "40", "--generation", "1"}).exitCode == kOk);
    REQUIRE(invoke({"thermal-load-add", cliPath(file), "2", "--generation", "2"}).exitCode == kOk);
    // Remove by the id that is NOT a valid position (there are three loads, so position 40 does not exist).
    REQUIRE(invoke({"thermal-load-remove", cliPath(file), "40"}).exitCode == kOk);
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    REQUIRE(d.loads.size() == 2U);
    CHECK(d.loads[0].id().value() == 1U);
    CHECK(d.loads[1].id().value() == 2U);
    // An id is stable across a save and load: a fresh process sees it unchanged.
    const Json j = parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out);
    CHECK(j["loads"][1]["loadId"].number() == 2.0);
}

TEST_CASE("ThermalCli_InvalidLoadFailsAtomically", "[cli][thermal][edit][load][atomic]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "invalid-load");
    const std::string before = bytesOf(file);
    const std::string end = faceText("end_cap");

    // THE CORE REFUSES (exit 1, a stable code, the file untouched).
    requireRefused(invoke({"thermal-load-add", cliPath(file), "1", "--generation", "5"}), "duplicate_id");
    requireRefused(invoke({"thermal-load-add", cliPath(file), "0", "--generation", "5"}), "invalid_value");
    requireRefused(invoke({"thermal-load-remove", cliPath(file), "99"}), "id_not_found");
    requireRefused(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "5", "--face", "face:NoSuchFeature:end_cap"}),
                   "bad_face_reference");
    // THE COMMAND LINE IS UNREADABLE (exit 2).
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "nan", "--face", end}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "inf", "--face", end}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "abc", "--face", end}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--generation", "1e5K"}));    // wrong dimension
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "5"}));               // no face
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--generation", "5", "--face", end}));   // face on a volume load
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "5", "--generation", "5", "--face", end}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--face", end}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "five", "--generation", "5"}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "--generation", "5"}));
    requireUsage(invoke({"thermal-load-remove", cliPath(file)}));
    requireUsage(invoke({"thermal-load-remove", cliPath(file), "x"}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--node", "12", "--flux", "5"}));    // no such option exists
    requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--facet", "12", "--flux", "5"}));
    CHECK(bytesOf(file) == before);
}

TEST_CASE("ThermalCli_LoadGeometryReferencePreserved", "[cli][thermal][edit][load][reference]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "load-reference");
    // A well-formed reference that names no face loads fine and is carried as intent: nothing rebinds it to the nearest face.
    REQUIRE(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "100", "--face", "face:Solid:side:9991"}).exitCode == kOk);
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    REQUIRE(d.loads.back().target().has_value());
    CHECK(d.loads.back().target()->face.role == FaceRole::Side);
    REQUIRE(d.loads.back().target()->face.entity.has_value());
    CHECK(d.loads.back().target()->face.entity->value() == 9991U);
    // And the file holds NO mesh identity.
    const std::string text = bytesOf(file);
    for (const char* forbidden : {"facet", "node", "dof", "temperatures", "gradient", "residual\":"}) {
        INFO(forbidden);
        CHECK(text.find(forbidden) == std::string::npos);
    }
}

// ===========================================================================
// Boundary conditions
// ===========================================================================

TEST_CASE("ThermalCli_AddPrescribedTemperature", "[cli][thermal][edit][boundary]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "add-temperature");
    const CliRun run = invoke({"thermal-boundary-add", cliPath(file), "7", sideText(study, 3), "--temperature", "412.25K"});
    REQUIRE(run.exitCode == kOk);
    CHECK_THAT(run.out, ContainsSubstring("Added boundary condition 7: prescribed temperature 412.25 K on"));
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    REQUIRE(d.boundaryConditions.size() == 3U);
    const ThermalBoundaryCondition& added = d.boundaryConditions.back();
    CHECK(added.id().value() == 7U);
    REQUIRE(added.prescribedTemperature() != nullptr);
    CHECK(bitsOf(added.prescribedTemperature()->value.si()) == bitsOf(412.25));
    CHECK(added.face() == study.side(3));
}

TEST_CASE("ThermalCli_AddConvection", "[cli][thermal][edit][boundary]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "add-convection");
    REQUIRE(invoke({"thermal-boundary-add", cliPath(file), "8", sideText(study, 0), "--convection", "12.5", "--ambient", "281.5"})
                .exitCode == kOk);
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    const ThermalBoundaryCondition& added = d.boundaryConditions.back();
    REQUIRE(added.convection() != nullptr);
    CHECK(bitsOf(added.convection()->coefficient.si()) == bitsOf(12.5));
    CHECK(bitsOf(added.convection()->ambient.si()) == bitsOf(281.5));
    CHECK(added.face() == study.side(0));
}

TEST_CASE("ThermalCli_RemoveBoundaryCondition", "[cli][thermal][edit][boundary]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "remove-boundary");
    REQUIRE(invoke({"thermal-boundary-remove", cliPath(file), "2"}).exitCode == kOk);
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto d = thermal::findThermalAnalysis(*loaded, study.analysis)->definition();
    REQUIRE(d.boundaryConditions.size() == 1U);
    CHECK(d.boundaryConditions.front().id().value() == 1U);
    requireRefused(invoke({"thermal-boundary-remove", cliPath(file), "2"}), "id_not_found");
}

TEST_CASE("ThermalCli_InvalidBoundaryConditionFailsAtomically", "[cli][thermal][edit][boundary][atomic]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "invalid-boundary");
    const std::string before = bytesOf(file);
    const std::string face = sideText(study, 1);

    // THE CORE REFUSES. None of these values is corrected, clamped or substituted.
    requireRefused(invoke({"thermal-boundary-add", cliPath(file), "1", face, "--temperature", "300"}), "duplicate_id");
    requireRefused(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--temperature", "-5"}), "invalid_value");
    requireRefused(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--convection", "0", "--ambient", "290"}), "invalid_value");
    requireRefused(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--convection", "-3", "--ambient", "290"}), "invalid_value");
    requireRefused(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--convection", "10", "--ambient", "-1"}), "invalid_value");
    requireRefused(invoke({"thermal-boundary-add", cliPath(file), "0", face, "--temperature", "300"}), "invalid_value");
    requireRefused(invoke({"thermal-boundary-add", cliPath(file), "5", "face:Nowhere:end_cap", "--temperature", "300"}),
                   "bad_face_reference");
    // THE COMMAND LINE IS UNREADABLE.
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--temperature", "nan"}));
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--temperature", "inf"}));
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--temperature", "20degC"}));   // no Celsius
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--temperature", "20 degF"}));
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--convection", "10"}));    // no ambient
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--temperature", "300", "--ambient", "290"}));
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face}));
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--temperature", "300", "--convection", "10",
                      "--ambient", "290"}));
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", "--temperature", "300"}));    // no face
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "x", face, "--temperature", "300"}));
    requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--convection", "10W", "--ambient", "290"}));
    requireUsage(invoke({"thermal-boundary-remove", cliPath(file)}));
    CHECK(bytesOf(file) == before);
}

TEST_CASE("ThermalCli_BoundaryGeometryReferencePreserved", "[cli][thermal][edit][boundary][reference]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "boundary-reference");
    REQUIRE(invoke({"thermal-boundary-add", cliPath(file), "5", "face:Solid:side:9991", "--convection", "10", "--ambient", "290"})
                .exitCode == kOk);
    const Json j = parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out);
    REQUIRE(j["boundaryConditions"].size() == 3U);
    CHECK(j["boundaryConditions"][2]["face"].text() == "face:Solid:side:9991");
    CHECK_FALSE(j["boundaryConditions"][2]["faceResolves"].boolean());
    // The solve REFUSES it and names the condition; nothing nearby is substituted.
    const CliRun solve = invoke({"thermal-solve", cliPath(file)});
    requireRefused(solve, "boundary_conditions_invalid");
    CHECK_THAT(solve.err, ContainsSubstring("Nothing nearby is substituted"));
}

TEST_CASE("ThermalCli_ConflictingPrescribedTemperaturesAreRefusedNotOrdered", "[cli][thermal][boundary][conflict]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "conflict");
    // The command layer permits it (it is canonical intent); the analysis refuses it, and neither picks the latest.
    REQUIRE(invoke({"thermal-boundary-add", cliPath(file), "5", faceText("start_cap"), "--temperature", "350"}).exitCode == kOk);
    requireRefused(invoke({"thermal-solve", cliPath(file)}), "boundary_conditions_invalid");
    const CliRun validate = invoke({"thermal-validate", cliPath(file), "--json"});
    CHECK(validate.exitCode == kFailed);
    const Json j = parseObject(validate.out);
    CHECK_FALSE(j["accepted"].boolean());
    CHECK(j["stoppedAt"].text() == "boundary");
}

// ===========================================================================
// Steady solve
// ===========================================================================

TEST_CASE("ThermalCli_SteadySolve", "[cli][thermal][solve][steady]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "steady");
    const CliRun run = invoke({"thermal-solve", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    CHECK(run.err.empty());
    const Json j = parseObject(run.out);

    const auto answer = coreAnswer(file);
    const thermal::ThermalAnalysisRun& core = *answer->run;
    REQUIRE(core.accepted());
    const CoreRange range = coreTemperatureRange(core);

    CHECK(j["mode"].text() == "steady");
    CHECK(j["nodeCount"].number() == static_cast<double>(core.dofMap->nodeCount()));
    CHECK(j["tetrahedronCount"].number() == static_cast<double>(core.post->elementCount()));
    CHECK(j["freeEquations"].number() == static_cast<double>(core.steady->freeEquations()));
    CHECK(j["constrainedCount"].number() == static_cast<double>(core.steady->constrainedCount()));
    CHECK(j["temperature"]["unit"].text() == "K");
    CHECK(j["temperature"]["count"].number() == static_cast<double>(core.dofMap->nodeCount()));
    CHECK(bitsOf(j["temperature"]["min"].quantity("K")) == bitsOf(range.low));
    CHECK(bitsOf(j["temperature"]["max"].quantity("K")) == bitsOf(range.high));
    CHECK(j["temperature"]["min"]["node"].number() == static_cast<double>(range.lowNode));
    CHECK(j["temperature"]["max"]["node"].number() == static_cast<double>(range.highNode));
    CHECK(bitsOf(j["normalizedResidual"].number()) == bitsOf(core.steady->residual().normalized));
    CHECK(bitsOf(j["pivotRatio"].number()) == bitsOf(core.steady->pivotRatio()));
    CHECK(j["heatBalance"]["balanced"].boolean() == core.balance->balanced());
    CHECK(bitsOf(j["heatBalance"]["worstNormalizedResidual"].number()) == bitsOf(core.balance->worstNormalizedResidual()));
    CHECK(j["validation"]["status"].text() == std::string{thermal::toString(core.validation.status())});
    CHECK(j["validation"]["status"].text() == "PASS");
    CHECK(j["validation"]["accepted"].boolean());
}

TEST_CASE("ThermalCli_SteadyRobinOnly", "[cli][thermal][solve][steady][well-posed]") {
    VizStudy study;
    // Convection anchors the model to an absolute temperature: the CLI adds no rule that a prescribed temperature is needed.
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)},
                                                   {vztest::convectionBc(1, study.endCap(), 25.0, vztest::kAmbient)}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "robin-only");
    const CliRun run = invoke({"thermal-solve", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    const Json j = parseObject(run.out);
    CHECK(j["constrainedCount"].number() == 0.0);
    CHECK(j["freeEquations"].number() == j["nodeCount"].number());
}

TEST_CASE("ThermalCli_FullyConstrainedSteady", "[cli][thermal][solve][steady][well-posed]") {
    VizStudy study;
    // A thin plate has no interior node, so holding every face holds every unknown. The CLI adds no "Nfree == 0" rule.
    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(
                    study.feature,
                    [](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = Length::fromSi(0.001);
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(study.regenerator, study.document);
    study.remesh();
    std::vector<ThermalBoundaryCondition> held{vztest::temperatureBc(1, study.startCap(), 320.0),
                                               vztest::temperatureBc(2, study.endCap(), 320.0)};
    for (std::size_t side = 0; side < 4; ++side) {
        held.push_back(vztest::temperatureBc(3 + side, study.side(side), 320.0));
    }
    vztest::define(study, vztest::steadyDefinition(study, {}, held));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "fully-constrained");
    const CliRun run = invoke({"thermal-solve", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    const Json j = parseObject(run.out);
    CHECK(j["freeEquations"].number() == 0.0);
    CHECK(j["constrainedCount"].number() == j["nodeCount"].number());
    CHECK(bitsOf(j["temperature"]["min"].quantity("K")) == bitsOf(320.0));
    CHECK(bitsOf(j["temperature"]["max"].quantity("K")) == bitsOf(320.0));
}

TEST_CASE("ThermalCli_SingularSteadyFails", "[cli][thermal][solve][steady][singular]") {
    VizStudy study;
    // Valid mesh, valid conductivity, and nothing to hold the temperature: pure Neumann.
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "singular");
    const std::string before = bytesOf(file);

    for (const char* command : {"thermal-solve", "thermal-temperature", "thermal-gradient", "thermal-heat-flux"}) {
        INFO(command);
        requireRefused(invoke({command, cliPath(file), "--json"}), "no_temperature_datum");
    }
    // The balance has no result to print: the chain stopped before it.
    requireRefused(invoke({"thermal-balance", cliPath(file), "--json"}), "no_temperature_datum");

    // The validation report IS the answer here: the payload is printed, names the rule, and the exit status still says no.
    const CliRun validate = invoke({"thermal-validate", cliPath(file), "--json"});
    CHECK(validate.exitCode == kFailed);
    const Json j = parseObject(validate.out);
    CHECK_FALSE(j["accepted"].boolean());
    CHECK(j["stoppedAt"].text() == "solve");
    CHECK(j["validation"]["status"].text() == "FAIL");
    bool found = false;
    for (std::size_t i = 0; i < j["validation"]["findings"].size(); ++i) {
        found = found || j["validation"]["findings"][i]["code"].text() == "no_temperature_datum";
    }
    CHECK(found);
    // NOTHING WAS REGULARISED: no result exists to have been produced, and the file is as it was.
    CHECK(bytesOf(file) == before);
}

// ===========================================================================
// Transient solve
// ===========================================================================

TEST_CASE("ThermalCli_TransientSolve", "[cli][thermal][solve][transient]") {
    VizStudy study;
    transientStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "transient");
    const CliRun run = invoke({"thermal-solve", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    const Json j = parseObject(run.out);
    const auto answer = coreAnswer(file);
    const thermal::ThermalAnalysisRun& core = *answer->run;
    REQUIRE(core.accepted());
    const CoreRange range = coreTemperatureRange(core);

    CHECK(j["mode"].text() == "transient");
    CHECK(j["stepCount"].number() == static_cast<double>(core.transient->grid().stepCount()));
    CHECK(j["storedStateCount"].number() == static_cast<double>(core.transient->sampleCount()));
    CHECK(bitsOf(j["startTime"].quantity("s")) == bitsOf(core.transient->grid().timeAt(0)));
    CHECK(bitsOf(j["endTime"].quantity("s")) == bitsOf(core.transient->grid().endTime()));
    CHECK(bitsOf(j["firstStoredTime"].quantity("s")) == bitsOf(core.transient->timeOf(0).si()));
    CHECK(bitsOf(j["lastStoredTime"].quantity("s")) == bitsOf(core.transient->timeOf(core.transient->sampleCount() - 1U).si()));
    CHECK(bitsOf(j["worstPhysicalResidual"].number()) == bitsOf(core.transient->worstPhysicalResidual()));
    CHECK(bitsOf(j["worstReducedResidual"].number()) == bitsOf(core.transient->worstReducedResidual()));
    CHECK(bitsOf(j["temperature"]["min"].quantity("K")) == bitsOf(range.low));
    CHECK(bitsOf(j["temperature"]["max"].quantity("K")) == bitsOf(range.high));
    CHECK(j["validation"]["accepted"].boolean());
}

TEST_CASE("ThermalCli_TransientPureNeumann", "[cli][thermal][solve][transient][well-posed]") {
    VizStudy study;
    // The steady datum rule must not be applied to a transient: heat with nowhere to go raises the temperature.
    vztest::define(study, vztest::transientDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}, 300.0, 1.0, 4.0));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "transient-neumann");
    const CliRun run = invoke({"thermal-solve", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    const Json j = parseObject(run.out);
    CHECK(j["constrainedCount"].number() == 0.0);
    // Heating raises the temperature above the initial value everywhere.
    CHECK(j["temperature"]["min"]["value"].number() > 300.0);
}

TEST_CASE("ThermalCli_TransientUsesActualStoredTimes", "[cli][thermal][transient][time]") {
    VizStudy study;
    vztest::define(study, vztest::transientDefinition(
                              study, {vztest::generationLoad(1, 1.0e5)}, {vztest::temperatureBc(1, study.startCap(), 300.0)}, 300.0,
                              0.3, 1.0));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "times");
    const Json j = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    const auto answer = coreAnswer(file);
    const thermal::ThermalAnalysisRun& core = *answer->run;
    REQUIRE(core.accepted());

    // THE TIMES ARE THE CORE'S STORED TIMES, not frame * dt: bit for bit.
    REQUIRE(j["history"].size() == core.transient->sampleCount());
    for (std::size_t i = 0; i < core.transient->sampleCount(); ++i) {
        INFO("state " << i);
        CHECK(bitsOf(j["history"][i]["time"].quantity("s")) == bitsOf(core.transient->timeOf(i).si()));
    }
    // 0.3 does not divide 1.0: the last stored time is 1.0, not 1.2.
    CHECK(j["history"].size() == 5U);
    CHECK(j["history"][4]["time"].quantity("s") == 1.0);
    CHECK(j["state"]["time"].quantity("s") == 1.0);
}

TEST_CASE("ThermalCli_TransientFinalPartialStep", "[cli][thermal][transient][time]") {
    VizStudy study;
    vztest::define(study, vztest::transientDefinition(
                              study, {vztest::generationLoad(1, 1.0e5)}, {vztest::temperatureBc(1, study.startCap(), 300.0)}, 300.0,
                              0.3, 1.0));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "partial");
    const Json balance = parseObject(invoke({"thermal-balance", cliPath(file), "--json"}).out);
    const std::size_t steps = balance["steps"].size();
    REQUIRE(steps == 4U);
    // THE LAST STEP IS THE REMAINDER, and the balance says so: its end is 1.0 and its size is the core's, not 0.3.
    CHECK(balance["steps"][steps - 1U]["time"].quantity("s") == 1.0);
    const double lastSize = balance["steps"][steps - 1U]["stepSize"].quantity("s");
    CHECK(lastSize < 0.3);
    CHECK(std::abs(lastSize - 0.1) < 1e-12);
    CHECK(balance["steps"][0]["stepSize"].quantity("s") == 0.3);
}

TEST_CASE("ThermalCli_TransientFinalOnlyHasOneStoredState", "[cli][thermal][transient][time]") {
    VizStudy study;
    // ONE step, so the final state and the initial state are all a step balance needs: the core accepts this history.
    transientStudy(study, 5.0, 5.0, thermal::TransientOutputPolicy::FinalOnly);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "final-only");
    const Json j = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    CHECK(j["history"].size() == 1U);
    CHECK(j["history"][0]["time"].quantity("s") == 5.0);
}

TEST_CASE("ThermalCli_TransientFinalOnlyWithManyStepsIsRefusedByTheCoreAndSaysWhy", "[cli][thermal][transient][failure]") {
    VizStudy study;
    // The heat balance of a backward-Euler step needs the states on both sides of it. A history that kept only the last one cannot
    // be balanced, and the analysis is therefore not accepted: the CLI neither fabricates the missing states nor drops the balance.
    transientStudy(study, 0.5, 5.0, thermal::TransientOutputPolicy::FinalOnly);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "final-only-many");
    const CliRun refused = invoke({"thermal-temperature", cliPath(file), "--json"});
    requireRefused(refused, "balance_missing");
    CHECK_THAT(refused.err, ContainsSubstring("EveryStep"));
    const CliRun validate = invoke({"thermal-validate", cliPath(file), "--json"});
    CHECK(validate.exitCode == kFailed);
    const Json j = parseObject(validate.out);
    CHECK(j["stoppedAt"].text() == "balance");
    CHECK_THAT(j["failure"].text(), ContainsSubstring("EveryStep"));
}

TEST_CASE("ThermalCli_TransientNeedsDensityAndSpecificHeat", "[cli][thermal][material][transient]") {
    VizStudy study;
    materials::ThermalProperties onlyK;
    onlyK.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, onlyK).has_value());
    transientStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "k-only-transient");
    requireRefused(invoke({"thermal-solve", cliPath(file)}), "material_incomplete_for_mode");
}

TEST_CASE("ThermalCli_SteadyNeedsOnlyConductivity", "[cli][thermal][material][steady]") {
    VizStudy study;
    materials::ThermalProperties onlyK;
    onlyK.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, onlyK).has_value());
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "k-only-steady");
    CHECK(invoke({"thermal-solve", cliPath(file)}).exitCode == kOk);
}

// ===========================================================================
// Summaries, equivalence with the core
// ===========================================================================

TEST_CASE("ThermalCli_TemperatureSummary", "[cli][thermal][summary][temperature]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "temperature");
    const Json j = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    const auto answer = coreAnswer(file);
    const CoreRange range = coreTemperatureRange(*answer->run);
    CHECK(j["temperature"]["unit"].text() == "K");
    CHECK(j["temperature"]["count"].number() == static_cast<double>(answer->run->dofMap->nodeCount()));
    CHECK(bitsOf(j["temperature"]["min"].quantity("K")) == bitsOf(range.low));
    CHECK(j["temperature"]["min"]["node"].number() == static_cast<double>(range.lowNode));
    CHECK(bitsOf(j["temperature"]["max"].quantity("K")) == bitsOf(range.high));
    CHECK(j["temperature"]["max"]["node"].number() == static_cast<double>(range.highNode));
    CHECK(j["state"]["index"].number() == 0.0);
    CHECK(j["state"]["time"].isNull());
    CHECK_FALSE(j.has("history"));   // a steady analysis has no history
    // The temperature is the SOLVER'S, bit for bit: the node the CLI named carries the value the CLI printed.
    const Result<Temperature> there = answer->run->post->temperatureOf(
        0, *answer->run->dofMap, meshing::NodeId::fromValue(static_cast<meshing::NodeId::ValueType>(range.lowNode)));
    REQUIRE(there.has_value());
    CHECK(bitsOf(there->si()) == bitsOf(range.low));
}

TEST_CASE("ThermalCli_TemperatureExtremaTieBreakIsTheAscendingNodeId", "[cli][thermal][summary][temperature][tie]") {
    VizStudy study;
    // Both caps held at the same temperature: many nodes share the minimum, and one must be named deterministically.
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)},
                                                   {vztest::temperatureBc(1, study.startCap(), 300.0),
                                                    vztest::temperatureBc(2, study.endCap(), 300.0)}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "tie");
    const Json j = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    const auto answer = coreAnswer(file);
    const auto& temperatures = answer->run->post->finalState().temperatures;
    std::uint64_t smallest = UINT64_MAX;
    std::size_t atMinimum = 0;
    for (std::size_t i = 0; i < temperatures.size(); ++i) {
        if (temperatures[i].si() == 300.0) {
            ++atMinimum;
            smallest = std::min<std::uint64_t>(
                smallest, answer->run->dofMap->nodeOf(thermal::ThermalDofIndex::fromValue(i))->value());
        }
    }
    REQUIRE(atMinimum > 1U);
    CHECK(j["temperature"]["min"]["value"].number() == 300.0);
    CHECK(j["temperature"]["min"]["node"].number() == static_cast<double>(smallest));
    // And it is the same node on every run.
    CHECK(invoke({"thermal-temperature", cliPath(file), "--json"}).out == invoke({"thermal-temperature", cliPath(file), "--json"}).out);
}

TEST_CASE("ThermalCli_GradientSummary", "[cli][thermal][summary][gradient]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "gradient");
    const Json j = parseObject(invoke({"thermal-gradient", cliPath(file), "--json"}).out);
    const auto answer = coreAnswer(file);
    const thermal::ThermalPostResult& post = *answer->run->post;
    const auto& gradients = post.finalState().fields.gradients;

    // The oracle: the largest norm of the core's stored gradient, ascending ElementId on a tie, found by this test's own loop.
    double best = -1.0;
    std::uint64_t bestElement = 0;
    double least = 0.0;
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        const double m = std::hypot(gradients[i].x.si(), gradients[i].y.si(), gradients[i].z.si());
        const std::uint64_t id = post.elementIds()[i].value();
        if (best < 0.0 || m > best || (m == best && id < bestElement)) {
            best = m;
            bestElement = id;
        }
        if (i == 0U || m < least) {
            least = m;
        }
    }
    CHECK(j["quantity"].text() == "temperature_gradient");
    CHECK(j["unit"].text() == "K/m");
    CHECK(j["domain"].text() == "element");
    CHECK(j["count"].number() == static_cast<double>(post.elementCount()));
    CHECK(j["tetrahedronCount"].number() == static_cast<double>(post.elementCount()));
    CHECK(bitsOf(j["magnitude"]["max"].number()) == bitsOf(best));
    CHECK(bitsOf(j["magnitude"]["min"].number()) == bitsOf(least));
    CHECK(j["largest"]["element"].number() == static_cast<double>(bestElement));
    // The SMALLEST too (a swap of "largest" and "smallest" must not pass), with the lowest id on a tie.
    std::uint64_t leastElement = 0;
    bool haveLeast = false;
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        const double m = std::hypot(gradients[i].x.si(), gradients[i].y.si(), gradients[i].z.si());
        const std::uint64_t id = post.elementIds()[i].value();
        if (m == least && (!haveLeast || id < leastElement)) {
            leastElement = id;
            haveLeast = true;
        }
    }
    REQUIRE(haveLeast);
    CHECK(j["smallest"]["element"].number() == static_cast<double>(leastElement));
    CHECK(bitsOf(j["smallest"]["magnitude"].quantity("K/m")) == bitsOf(least));
    CHECK(j["magnitude"]["unit"].text() == "K/m");
    CHECK(bitsOf(j["largest"]["magnitude"].quantity("K/m")) == bitsOf(best));
    // The vector is the core's stored components for that element, unrecomputed and unsigned-changed.
    const std::size_t at = *post.ordinalOf(meshing::ElementId::fromValue(static_cast<meshing::ElementId::ValueType>(bestElement)));
    CHECK(bitsOf(j["largest"]["gradient"]["x"].number()) == bitsOf(gradients[at].x.si()));
    CHECK(bitsOf(j["largest"]["gradient"]["y"].number()) == bitsOf(gradients[at].y.si()));
    CHECK(bitsOf(j["largest"]["gradient"]["z"].number()) == bitsOf(gradients[at].z.si()));
}

TEST_CASE("ThermalCli_HeatFluxSummary", "[cli][thermal][summary][heat-flux]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "heat-flux");
    const Json j = parseObject(invoke({"thermal-heat-flux", cliPath(file), "--json"}).out);
    const auto answer = coreAnswer(file);
    const thermal::ThermalPostResult& post = *answer->run->post;
    const auto& fields = post.finalState().fields;

    double best = -1.0;
    std::uint64_t bestElement = 0;
    for (std::size_t i = 0; i < fields.heatFluxMagnitudes.size(); ++i) {
        const double m = fields.heatFluxMagnitudes[i].si();
        const std::uint64_t id = post.elementIds()[i].value();
        if (best < 0.0 || m > best || (m == best && id < bestElement)) {
            best = m;
            bestElement = id;
        }
    }
    // THE CONDUCTIVE FLUX, labelled as such, in W/m^2 -- not the applied surface flux of a load.
    CHECK(j["quantity"].text() == "conductive_heat_flux");
    CHECK(j["unit"].text() == "W/m^2");
    CHECK(j["count"].number() == static_cast<double>(post.elementCount()));
    CHECK(bitsOf(j["magnitude"]["max"].number()) == bitsOf(best));
    CHECK(j["largest"]["element"].number() == static_cast<double>(bestElement));
    CHECK(bitsOf(j["largest"]["magnitude"].quantity("W/m^2")) == bitsOf(best));
    double least = fields.heatFluxMagnitudes.front().si();
    std::uint64_t leastElement = post.elementIds().front().value();
    for (std::size_t i = 0; i < fields.heatFluxMagnitudes.size(); ++i) {
        const double m = fields.heatFluxMagnitudes[i].si();
        const std::uint64_t id = post.elementIds()[i].value();
        if (m < least || (m == least && id < leastElement)) {
            least = m;
            leastElement = id;
        }
    }
    CHECK(bitsOf(j["magnitude"]["min"].number()) == bitsOf(least));
    CHECK(j["smallest"]["element"].number() == static_cast<double>(leastElement));
    CHECK(bitsOf(j["smallest"]["magnitude"].quantity("W/m^2")) == bitsOf(least));

    // NO SIGN INVERSION: the vector is the core's, and it points down the gradient (q . grad(T) <= 0, Fourier's law).
    const std::size_t at = *post.ordinalOf(meshing::ElementId::fromValue(static_cast<meshing::ElementId::ValueType>(bestElement)));
    const auto& q = fields.heatFluxes[at];
    CHECK(bitsOf(j["largest"]["flux"]["x"].number()) == bitsOf(q.x.si()));
    CHECK(bitsOf(j["largest"]["flux"]["y"].number()) == bitsOf(q.y.si()));
    CHECK(bitsOf(j["largest"]["flux"]["z"].number()) == bitsOf(q.z.si()));
    const auto& g = post.finalState().fields.gradients[at];
    CHECK(q.x.si() * g.x.si() + q.y.si() * g.y.si() + q.z.si() * g.z.si() <= 0.0);
}

TEST_CASE("ThermalCli_HeatBalanceReport", "[cli][thermal][summary][balance]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "balance");
    const CliRun run = invoke({"thermal-balance", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    const Json j = parseObject(run.out);
    const auto answer = coreAnswer(file);
    const thermal::ThermalHeatBalance& core = answer->run->balance->finalState();

    CHECK(j["balanced"].boolean());
    CHECK(j["accepted"].boolean());
    CHECK(j["stateCount"].number() == 1.0);
    CHECK(bitsOf(j["final"]["surfaceHeatInto"].quantity("W")) == bitsOf(core.terms.appliedSurfaceHeat.si()));
    CHECK(bitsOf(j["final"]["generationInto"].quantity("W")) == bitsOf(core.terms.generatedHeat.si()));
    CHECK(bitsOf(j["final"]["convectionInto"].quantity("W")) == bitsOf(core.terms.convectionHeat.si()));
    CHECK(bitsOf(j["final"]["dirichletReactionInto"].quantity("W")) == bitsOf(core.terms.prescribedTemperatureReaction.si()));
    CHECK(bitsOf(j["final"]["residual"].quantity("W")) == bitsOf(core.residual().si()));
    CHECK(bitsOf(j["final"]["normalizedResidual"].number()) == bitsOf(core.normalizedResidual()));
    CHECK(j["final"]["status"].text() == std::string{thermal::toString(core.status)});
    CHECK_FALSE(j["final"].has("storedEnergyRate"));   // there is no storage term in a steady balance
    // The attribution is by canonical id.
    REQUIRE(j["loads"].size() == core.surfaceLoads.size() + core.generationLoads.size());
    CHECK(j["loads"][0]["loadId"].number() == 1.0);
    CHECK(bitsOf(j["loads"][0]["heatInto"].quantity("W")) == bitsOf(core.generationLoads.front().heatInto.si()));
    REQUIRE(j["convections"].size() == 1U);
    CHECK(j["convections"][0]["boundaryConditionId"].number() == 2.0);
    CHECK(j["reactionCount"].number() == static_cast<double>(core.reactions.size()));
    // A BOUND, not a closed form: what is generated leaves through the held cap and the convecting one, so the residual is rounding.
    CHECK(std::abs(core.residual().si()) < 1e-6);
}

TEST_CASE("ThermalCli_HeatBalanceReportTransientIsPerStep", "[cli][thermal][summary][balance][transient]") {
    VizStudy study;
    transientStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "balance-transient");
    const Json j = parseObject(invoke({"thermal-balance", cliPath(file), "--json"}).out);
    const auto answer = coreAnswer(file);
    const thermal::ThermalBalanceResult& core = *answer->run->balance;
    REQUIRE(j["steps"].size() == core.stateCount());
    for (std::size_t i = 0; i < core.stateCount(); ++i) {
        INFO("step " << i);
        const thermal::ThermalHeatBalance& s = core.states()[i];
        CHECK(j["steps"][i]["step"].number() == static_cast<double>(s.step));
        CHECK(bitsOf(j["steps"][i]["time"].quantity("s")) == bitsOf(s.time->si()));
        CHECK(bitsOf(j["steps"][i]["stepSize"].quantity("s")) == bitsOf(s.stepSize->si()));
        CHECK(bitsOf(j["steps"][i]["storedEnergyRate"].quantity("W")) == bitsOf(s.terms.storedEnergyRate->si()));
        CHECK(bitsOf(j["steps"][i]["storedEnergyChange"].quantity("J")) == bitsOf(s.storedEnergyChange->si()));
        CHECK(bitsOf(j["steps"][i]["normalizedResidual"].number()) == bitsOf(s.normalizedResidual()));
    }
    REQUIRE(core.integrated().has_value());
    CHECK(bitsOf(j["integrated"]["external"].quantity("J")) == bitsOf(core.integrated()->external.si()));
    CHECK(bitsOf(j["integrated"]["stored"].quantity("J")) == bitsOf(core.integrated()->stored.si()));
    CHECK(bitsOf(j["worstNormalizedResidual"].number()) == bitsOf(core.worstNormalizedResidual()));
}

TEST_CASE("ThermalCli_ValidationReport", "[cli][thermal][summary][validation]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "validation");
    const CliRun run = invoke({"thermal-validate", cliPath(file), "--json"});
    REQUIRE(run.exitCode == kOk);
    const Json j = parseObject(run.out);
    const auto answer = coreAnswer(file);
    const thermal::ThermalValidationReport& core = answer->run->validation;

    CHECK(j["accepted"].boolean());
    CHECK(j["stoppedAt"].isNull());
    CHECK(j["failure"].isNull());
    const Json& v = j["validation"];
    CHECK(v["status"].text() == std::string{thermal::toString(core.status())});
    CHECK(v["accepted"].boolean() == core.accepted());
    CHECK(v["policy"].text() == core.policyFingerprint);
    // EVERY RULE, IN THE CORE'S ORDER, WITH THE CORE'S STATUS -- not a bare PASS.
    REQUIRE(v["checks"].size() == core.checks.size());
    REQUIRE(core.checks.size() == thermal::kValidationRuleCount);
    for (std::size_t i = 0; i < core.checks.size(); ++i) {
        INFO("check " << i);
        CHECK(v["checks"][i]["rule"].text() == std::string{thermal::toString(core.checks[i].rule)});
        CHECK(v["checks"][i]["status"].text() == std::string{thermal::toString(core.statusOf(core.checks[i].rule))});
        CHECK(v["checks"][i]["group"].text() == std::string{thermal::toString(thermal::groupOf(core.checks[i].rule))});
    }
    REQUIRE(v["findings"].size() == core.findings.size());
    for (std::size_t i = 0; i < core.findings.size(); ++i) {
        CHECK(v["findings"][i]["code"].text() == std::string{thermal::toString(core.findings[i].code)});
        CHECK(v["findings"][i]["severity"].text() == std::string{thermal::toString(core.findings[i].severity)});
    }
    CHECK(v["errorCount"].number() == static_cast<double>(core.errorCount()));
    CHECK(v["warningCount"].number() == static_cast<double>(core.warningCount()));
}

TEST_CASE("ThermalCli_WarningOnlyReportExitsZeroAndKeepsTheWarning", "[cli][thermal][summary][validation][warning]") {
    // An INFORMATION finding is not a warning, but the same exit policy governs both: the gate is the core's `accepted()`, which a
    // report with findings that do not refuse satisfies. A transient model with no datum carries one such finding.
    VizStudy study;
    vztest::define(study, vztest::transientDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}, 300.0, 1.0, 4.0));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "info-finding");
    const CliRun run = invoke({"thermal-validate", cliPath(file), "--json"});
    CHECK(run.exitCode == kOk);
    const Json j = parseObject(run.out);
    CHECK(j["accepted"].boolean());
    bool seen = false;
    for (std::size_t i = 0; i < j["validation"]["findings"].size(); ++i) {
        if (j["validation"]["findings"][i]["code"].text() == "transient_without_datum") {
            seen = true;
            CHECK(j["validation"]["findings"][i]["severity"].text() == "info");
        }
    }
    CHECK(seen);
}

// ===========================================================================
// Failures
// ===========================================================================

TEST_CASE("ThermalCli_MissingMaterialFails", "[cli][thermal][failure][material]") {
    VizStudy study;
    steadyStudy(study);
    REQUIRE(features::removeMaterialAssignment(study.document).has_value());
    const TempDir temp;
    const auto file = saveStudy(study, temp, "no-material");
    const std::string before = bytesOf(file);
    for (const char* command : {"thermal-solve", "thermal-temperature", "thermal-gradient", "thermal-heat-flux"}) {
        INFO(command);
        requireRefused(invoke({command, cliPath(file), "--json"}), "material_missing");
    }
    // The validation report names it, and no default material was supplied.
    const CliRun validate = invoke({"thermal-validate", cliPath(file), "--json"});
    CHECK(validate.exitCode == kFailed);
    const Json j = parseObject(validate.out);
    CHECK(j["stoppedAt"].text() == "model");
    CHECK(j["validation"]["findings"][0]["code"].text() == "material_missing");
    CHECK(bytesOf(file) == before);
    // No solver ran: nothing was built past the model.
    CHECK(j["validation"]["checks"].size() == thermal::kValidationRuleCount);
}

TEST_CASE("ThermalCli_InvalidLoadFails", "[cli][thermal][failure][load]") {
    VizStudy study;
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}};
    vztest::define(study, vztest::steadyDefinition(study, {vztest::surfaceLoad(1, nowhere, 500.0)},
                                                   {vztest::temperatureBc(1, study.startCap(), 300.0)}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "invalid-load-solve");
    const std::string before = bytesOf(file);
    const CliRun solve = invoke({"thermal-solve", cliPath(file)});
    requireRefused(solve, "loads_invalid");
    CHECK_THAT(solve.err, ContainsSubstring("Nothing nearby is substituted"));
    // The offending entity is not dropped.
    const Json j = parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out);
    CHECK(j["loads"].size() == 1U);
    CHECK(bytesOf(file) == before);
}

TEST_CASE("ThermalCli_InvalidBoundaryConditionFails", "[cli][thermal][failure][boundary]") {
    VizStudy study;
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}};
    vztest::define(study, vztest::steadyDefinition(study, {}, {vztest::temperatureBc(1, study.startCap(), 300.0),
                                                              vztest::convectionBc(2, nowhere, 10.0, 290.0)}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "invalid-boundary-solve");
    requireRefused(invoke({"thermal-solve", cliPath(file)}), "boundary_conditions_invalid");
    const Json j = parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out);
    CHECK(j["boundaryConditions"].size() == 2U);
}

TEST_CASE("ThermalCli_AMeshThatCannotBeGeneratedIsTheCliAnalogueOfAStaleMesh", "[cli][thermal][failure][mesh]") {
    // A sizing reference that names no face means no mesh can be generated from the document's intent. This is the stale-mesh
    // refusal AS A ONE-SHOT PROCESS CAN PRESENT IT: the CLI regenerates and remeshes every time, so a mesh that does not describe the
    // model is never handed to the solve, and a mesh that cannot be built is reported as itself.
    VizStudy study;
    steadyStudy(study);
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}};
    REQUIRE(study.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(study.control.value()),
                    [&](meshing::MeshControl& control) -> Result<bool> {
                        meshing::MeshControlDefinition definition = control.definition();
                        definition.mesh.sizing.local.push_back(
                            meshing::LocalMeshSizing{.face = nowhere, .targetSize = Length::fromSi(0.005)});
                        return control.setDefinition(definition).has_value();
                    })
                .has_value());
    const TempDir temp;
    const auto file = saveStudy(study, temp, "no-mesh");
    for (const char* command : {"thermal-solve", "thermal-validate", "thermal-balance", "thermal-temperature"}) {
        INFO(command);
        requireRefused(invoke({command, cliPath(file), "--json"}), "mesh_generation_failed");
    }
}

TEST_CASE("ThermalCli_AFileThatIsNotThereIsADocumentFailure", "[cli][thermal][failure][document]") {
    const TempDir temp;
    const std::string missing = cliPath(temp.path() / "missing.bcad");
    for (const char* command : {"thermal-settings", "thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient",
                                "thermal-heat-flux", "thermal-balance"}) {
        INFO(command);
        requireRefused(invoke({command, missing, "--json"}), "document_not_loaded");
    }
    // An edit of a file that is not there fails too, and creates nothing.
    CHECK(invoke({"thermal-load-add", missing, "1", "--generation", "5"}).exitCode == kFailed);
    CHECK_FALSE(std::filesystem::exists(temp.path() / "missing.bcad"));
}

// ===========================================================================
// Core equivalence of the results
// ===========================================================================

TEST_CASE("ThermalCli_SteadyCoreEquivalence", "[cli][thermal][equivalence][steady]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "equivalence-steady");

    // ONE FILE, SIX COMMANDS, ONE CORE RUN. The commands are separate invocations and each ran the analysis for itself; every
    // number they print agrees with the others and with the core's arrays, which is only possible if each is a view of one result.
    const auto answer = coreAnswer(file);
    const thermal::ThermalAnalysisRun& core = *answer->run;
    REQUIRE(core.accepted());
    const CoreRange range = coreTemperatureRange(core);
    const auto& fields = core.post->finalState().fields;

    const Json solve = parseObject(invoke({"thermal-solve", cliPath(file), "--json"}).out);
    const Json temperature = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    const Json gradient = parseObject(invoke({"thermal-gradient", cliPath(file), "--json"}).out);
    const Json flux = parseObject(invoke({"thermal-heat-flux", cliPath(file), "--json"}).out);
    const Json balance = parseObject(invoke({"thermal-balance", cliPath(file), "--json"}).out);
    const Json validation = parseObject(invoke({"thermal-validate", cliPath(file), "--json"}).out);

    CHECK(bitsOf(solve["temperature"]["min"].quantity("K")) == bitsOf(range.low));
    CHECK(bitsOf(temperature["temperature"]["min"].quantity("K")) == bitsOf(range.low));
    CHECK(bitsOf(solve["temperature"]["max"].quantity("K")) == bitsOf(range.high));
    CHECK(bitsOf(temperature["temperature"]["max"].quantity("K")) == bitsOf(range.high));

    // The largest gradient and flux: found here by a separate scan of the core's FULL arrays.
    double maxGradient = 0.0;
    double maxFlux = 0.0;
    for (std::size_t i = 0; i < fields.gradients.size(); ++i) {
        maxGradient = std::max(maxGradient, std::hypot(fields.gradients[i].x.si(), fields.gradients[i].y.si(), fields.gradients[i].z.si()));
        maxFlux = std::max(maxFlux, fields.heatFluxMagnitudes[i].si());
    }
    CHECK(bitsOf(gradient["magnitude"]["max"].number()) == bitsOf(maxGradient));
    CHECK(bitsOf(flux["magnitude"]["max"].number()) == bitsOf(maxFlux));

    // The reactions, the balance and the validation.
    CHECK(bitsOf(balance["final"]["dirichletReactionInto"].quantity("W")) ==
          bitsOf(core.balance->finalState().terms.prescribedTemperatureReaction.si()));
    CHECK(bitsOf(balance["worstNormalizedResidual"].number()) == bitsOf(core.balance->worstNormalizedResidual()));
    CHECK(solve["validation"]["status"].text() == validation["validation"]["status"].text());
    CHECK(validation["validation"]["status"].text() == std::string{thermal::toString(core.validation.status())});
    CHECK(solve["nodeCount"].number() == static_cast<double>(core.dofMap->nodeCount()));
}

TEST_CASE("ThermalCli_TransientCoreEquivalence", "[cli][thermal][equivalence][transient]") {
    VizStudy study;
    transientStudy(study, 0.5, 5.0);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "equivalence-transient");
    const auto answer = coreAnswer(file);
    const thermal::ThermalAnalysisRun& core = *answer->run;
    REQUIRE(core.accepted());

    const Json temperature = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    const thermal::ThermalPostResult& post = *core.post;
    // EVERY STORED STATE, against the core's arrays: its time (the core's, not frame * dt) and the range of its full temperature
    // vector.
    REQUIRE(temperature["history"].size() == post.stateCount());
    REQUIRE(post.stateCount() == core.transient->sampleCount());
    for (std::size_t i = 0; i < post.stateCount(); ++i) {
        INFO("state " << i);
        const auto& state = post.states()[i];
        double low = state.temperatures.front().si();
        double high = low;
        for (const Temperature& t : state.temperatures) {
            low = std::min(low, t.si());
            high = std::max(high, t.si());
        }
        CHECK(bitsOf(temperature["history"][i]["time"].quantity("s")) == bitsOf(state.time->si()));
        CHECK(bitsOf(temperature["history"][i]["time"].quantity("s")) == bitsOf(core.transient->timeOf(i).si()));
        CHECK(bitsOf(temperature["history"][i]["min"].quantity("K")) == bitsOf(low));
        CHECK(bitsOf(temperature["history"][i]["max"].quantity("K")) == bitsOf(high));
    }
    // The first state is the initial condition, and no node is below the lower of the initial and the held temperature.
    CHECK(temperature["history"][0]["min"].quantity("K") <= temperature["history"][0]["max"].quantity("K"));
    CHECK(temperature["history"][0]["time"].quantity("s") == 0.0);
    // The final state's figures are the summary's.
    const std::size_t last = post.stateCount() - 1U;
    CHECK(bitsOf(temperature["temperature"]["min"].quantity("K")) == bitsOf(temperature["history"][last]["min"].quantity("K")));
    CHECK(bitsOf(temperature["temperature"]["max"].quantity("K")) == bitsOf(temperature["history"][last]["max"].quantity("K")));

    // The gradient and the flux are the final state's, and the balance has one entry per integration step.
    const Json gradient = parseObject(invoke({"thermal-gradient", cliPath(file), "--json"}).out);
    double maxGradient = 0.0;
    for (const auto& g : post.finalState().fields.gradients) {
        maxGradient = std::max(maxGradient, std::hypot(g.x.si(), g.y.si(), g.z.si()));
    }
    CHECK(bitsOf(gradient["magnitude"]["max"].number()) == bitsOf(maxGradient));
    const Json balance = parseObject(invoke({"thermal-balance", cliPath(file), "--json"}).out);
    CHECK(balance["steps"].size() == core.transient->grid().stepCount());
    CHECK(balance["stateCount"].number() == static_cast<double>(core.balance->stateCount()));
    const Json validation = parseObject(invoke({"thermal-validate", cliPath(file), "--json"}).out);
    CHECK(validation["validation"]["status"].text() == std::string{thermal::toString(core.validation.status())});
}
// ===========================================================================
// Core equivalence of the edits
// ===========================================================================

TEST_CASE("ThermalCli_AddLoadCoreEquivalence", "[cli][thermal][equivalence][edit]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto original = saveStudy(study, temp, "baseline");
    const auto viaCli = copyOf(original, temp, "via-cli");
    const auto viaCore = copyOf(original, temp, "via-core");

    REQUIRE(invoke({"thermal-load-add", cliPath(viaCli), "4", "--flux", "750.5", "--face", sideText(study, 2)}).exitCode == kOk);

    Result<Document> loaded = io::loadDocument(viaCore);
    REQUIRE(loaded.has_value());
    thermal::AddThermalLoadCommand command{study.analysis, vztest::surfaceLoad(4, study.side(2), 750.5)};
    REQUIRE(command.execute(*loaded).has_value());
    REQUIRE(io::saveDocument(*loaded, viaCore).has_value());

    // THE STRONGEST FORM: the two canonical files are the same bytes. The CLI built the same command the core would.
    CHECK(bytesOf(viaCli) == bytesOf(viaCore));
    CHECK(bytesOf(viaCli) != bytesOf(original));
}

TEST_CASE("ThermalCli_RemoveLoadCoreEquivalence", "[cli][thermal][equivalence][edit]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto original = saveStudy(study, temp, "baseline-remove");
    const auto viaCli = copyOf(original, temp, "remove-via-cli");
    const auto viaCore = copyOf(original, temp, "remove-via-core");
    REQUIRE(invoke({"thermal-load-remove", cliPath(viaCli), "1"}).exitCode == kOk);
    Result<Document> loaded = io::loadDocument(viaCore);
    REQUIRE(loaded.has_value());
    thermal::RemoveThermalLoadCommand command{study.analysis, ThermalLoadId::fromValue(1U)};
    REQUIRE(command.execute(*loaded).has_value());
    REQUIRE(io::saveDocument(*loaded, viaCore).has_value());
    CHECK(bytesOf(viaCli) == bytesOf(viaCore));
}

TEST_CASE("ThermalCli_AddBoundaryConditionCoreEquivalence", "[cli][thermal][equivalence][edit]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto original = saveStudy(study, temp, "baseline-boundary");
    for (const bool convection : {false, true}) {
        INFO((convection ? "convection" : "prescribed temperature"));
        const auto viaCli = copyOf(original, temp, convection ? "bc-cli-h" : "bc-cli-t");
        const auto viaCore = copyOf(original, temp, convection ? "bc-core-h" : "bc-core-t");
        if (convection) {
            REQUIRE(invoke({"thermal-boundary-add", cliPath(viaCli), "6", sideText(study, 1), "--convection", "17.5", "--ambient",
                         "288.15"})
                        .exitCode == kOk);
        } else {
            REQUIRE(invoke({"thermal-boundary-add", cliPath(viaCli), "6", sideText(study, 1), "--temperature", "333.25"}).exitCode ==
                    kOk);
        }
        Result<Document> loaded = io::loadDocument(viaCore);
        REQUIRE(loaded.has_value());
        thermal::AddThermalBoundaryConditionCommand command{
            study.analysis, convection ? vztest::convectionBc(6, study.side(1), 17.5, 288.15)
                                       : vztest::temperatureBc(6, study.side(1), 333.25)};
        REQUIRE(command.execute(*loaded).has_value());
        REQUIRE(io::saveDocument(*loaded, viaCore).has_value());
        CHECK(bytesOf(viaCli) == bytesOf(viaCore));
    }
}

TEST_CASE("ThermalCli_RemoveBoundaryConditionCoreEquivalence", "[cli][thermal][equivalence][edit]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto original = saveStudy(study, temp, "baseline-remove-boundary");
    const auto viaCli = copyOf(original, temp, "rb-cli");
    const auto viaCore = copyOf(original, temp, "rb-core");
    REQUIRE(invoke({"thermal-boundary-remove", cliPath(viaCli), "2"}).exitCode == kOk);
    Result<Document> loaded = io::loadDocument(viaCore);
    REQUIRE(loaded.has_value());
    thermal::RemoveThermalBoundaryConditionCommand command{study.analysis, ThermalBoundaryConditionId::fromValue(2U)};
    REQUIRE(command.execute(*loaded).has_value());
    REQUIRE(io::saveDocument(*loaded, viaCore).has_value());
    CHECK(bytesOf(viaCli) == bytesOf(viaCore));
}

TEST_CASE("ThermalCli_AddAnalysisCoreEquivalence", "[cli][thermal][equivalence][edit]") {
    const TempDir temp;
    const auto viaCli = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "analysis-cli");
    const auto viaCore = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "analysis-core");
    REQUIRE(invoke({"thermal-analysis-add", cliPath(viaCli), "--name", "Heating", "--mode", "transient", "--initial-temperature",
                 "295.5", "--time-step", "0.25", "--end-time", "3.5", "--output", "every-step"})
                .exitCode == kOk);
    Result<Document> loaded = io::loadDocument(viaCore);
    REQUIRE(loaded.has_value());
    const auto controls = meshing::meshControls(*loaded);
    REQUIRE(controls.size() == 1U);
    thermal::ThermalAnalysisDefinition definition;
    definition.mesh = controls.front();
    definition.mode = ThermalAnalysisMode::Transient;
    definition.initialCondition = thermal::UniformInitialTemperature{Temperature::fromSi(295.5)};
    definition.transient = thermal::TransientSettings{.timeStep = Time::fromSi(0.25), .endTime = Time::fromSi(3.5),
                                                      .output = thermal::TransientOutputPolicy::EveryStep};
    thermal::CreateThermalAnalysisCommand command{"Heating", definition};
    REQUIRE(command.execute(*loaded).has_value());
    REQUIRE(io::saveDocument(*loaded, viaCore).has_value());
    CHECK(bytesOf(viaCli) == bytesOf(viaCore));
}

// ===========================================================================
// Persistence and process isolation
// ===========================================================================

TEST_CASE("ThermalCli_SolveDoesNotModifyCanonicalBytes", "[cli][thermal][persistence][bytes]") {
    for (const bool transient : {false, true}) {
        INFO((transient ? "transient" : "steady"));
        VizStudy study;
        if (transient) {
            transientStudy(study);
        } else {
            steadyStudy(study);
        }
        const TempDir temp;
        const auto file = saveStudy(study, temp, "bytes");
        const std::string before = bytesOf(file);
        const auto sizeBefore = std::filesystem::file_size(file);
        for (const char* command : {"thermal-solve", "thermal-temperature", "thermal-gradient", "thermal-heat-flux",
                                    "thermal-balance", "thermal-validate", "thermal-settings"}) {
            REQUIRE(invoke({command, cliPath(file), "--json"}).exitCode == kOk);
            CHECK(bytesOf(file) == before);
            CHECK(std::filesystem::file_size(file) == sizeBefore);
        }
    }
}

TEST_CASE("ThermalCli_SaveLoadResolveAgreesAcrossFreshRuns", "[cli][thermal][persistence][resolve]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "resolve");
    // Edit through the CLI (a save), then solve twice: each solve is a fresh load, regenerate, mesh and run. The payloads are
    // byte-identical, and nothing was carried between them.
    REQUIRE(invoke({"thermal-load-add", cliPath(file), "6", "--flux", "250", "--face", faceText("end_cap")}).exitCode == kOk);
    const std::string first = invoke({"thermal-solve", cliPath(file), "--json"}).out;
    const std::string second = invoke({"thermal-solve", cliPath(file), "--json"}).out;
    CHECK(!first.empty());
    CHECK(first == second);
}

TEST_CASE("ThermalCli_AnEditChangesTheNextSolveAndNothingStaleSurvives", "[cli][thermal][persistence][currentness]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "edit-then-solve");
    const Json before = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    REQUIRE(invoke({"thermal-load-remove", cliPath(file), "1"}).exitCode == kOk);
    const Json after = parseObject(invoke({"thermal-temperature", cliPath(file), "--json"}).out);
    // The generation was removed: the body is no longer heated, so the peak temperature falls. A CLI that remembered the first
    // result would print the same number.
    CHECK(after["temperature"]["max"]["value"].number() < before["temperature"]["max"]["value"].number());
}

// ===========================================================================
// Standard output and exit codes
// ===========================================================================

TEST_CASE("ThermalCli_StructuredStdoutIsClean", "[cli][thermal][stdout]") {
    for (const bool transient : {false, true}) {
        VizStudy study;
        if (transient) {
            transientStudy(study);
        } else {
            steadyStudy(study);
        }
        const TempDir temp;
        const auto file = saveStudy(study, temp, "stdout");
        for (const char* command : {"thermal-settings", "thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient",
                                    "thermal-heat-flux", "thermal-balance"}) {
            INFO(command << (transient ? " (transient)" : " (steady)"));
            const CliRun run = invoke({command, cliPath(file), "--json"});
            REQUIRE(run.exitCode == kOk);
            // EXACTLY ONE TOP-LEVEL OBJECT and nothing else: no banner before it, no summary after it, no ANSI escape.
            std::string why;
            const std::optional<Json> parsed = JsonReader::parse(run.out, &why);
            INFO(why);
            REQUIRE(parsed.has_value());
            CHECK(parsed->isObject());
            CHECK(run.out.front() == '{');
            CHECK(run.out.back() == '\n');
            CHECK(run.out.find('\x1b') == std::string::npos);
            CHECK(run.err.empty());
        }
    }
}

TEST_CASE("ThermalCli_StructuredFailureOutputIsClean", "[cli][thermal][stdout][failure]") {
    // A command that cannot answer prints NOTHING to stdout (the stream discipline), and its code is on stderr in one line.
    VizStudy study;
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "failure-stdout");
    for (const char* command : {"thermal-solve", "thermal-temperature", "thermal-gradient", "thermal-heat-flux", "thermal-balance"}) {
        const CliRun run = invoke({command, cliPath(file), "--json"});
        CHECK(run.exitCode == kFailed);
        CHECK(run.out.empty());
        CHECK(std::count(run.err.begin(), run.err.end(), '\n') == 1);
        CHECK(run.err.find('\x1b') == std::string::npos);
    }
    // The two reports that are themselves the answer print a parseable payload AND fail.
    const CliRun validate = invoke({"thermal-validate", cliPath(file), "--json"});
    CHECK(validate.exitCode == kFailed);
    CHECK(validate.err.empty());
    REQUIRE(JsonReader::parse(validate.out).has_value());
}

TEST_CASE("ThermalCli_NonFiniteNumbersAreNeverWrittenAsNumbers", "[cli][thermal][stdout][nonfinite]") {
    cli::FiniteJson fin;
    cli::JsonValue payload = cli::JsonValue::object();
    payload.set("a", fin.number(std::nan("")));
    payload.set("b", fin.number(HUGE_VAL));
    payload.set("c", fin.number(-HUGE_VAL));
    payload.set("quantity", fin.quantity(std::nan(""), "K"));
    payload.set("fine", fin.number(2.5));
    CHECK(fin.sawNonFinite());
    // The strict reader is the gate: `nan`, `inf` and `-inf` are not JSON, so a payload that held one would not parse at all.
    const std::string text = payload.render();
    const Json parsed = parseObject(text);
    CHECK(parsed["a"].isNull());
    CHECK(parsed["b"].isNull());
    CHECK(parsed["c"].isNull());
    CHECK(parsed["quantity"]["value"].isNull());
    CHECK(parsed["fine"].number() == 2.5);

    cli::FiniteJson clean;
    (void)clean.number(1.0);
    CHECK_FALSE(clean.sawNonFinite());
}

TEST_CASE("ThermalCli_ANonFiniteMeasurementInTheReportKeepsAParseablePayload", "[cli][thermal][stdout][nonfinite]") {
    // A real document that overflows: the temperatures are not finite, the solve refuses, and the report that explains why is strict
    // JSON whose exit status agrees with its own `accepted` field. (The core refuses every overflow before a non-finite value can reach
    // a printer, so the `valueFinite: false` branch itself is exercised on a hand-built report in
    // ThermalCli_ValidationJsonWritesANonFiniteMeasurementAsNullAndSaysSo.)
    VizStudy study;
    vztest::define(study, vztest::steadyDefinition(study, {vztest::surfaceLoad(1, study.endCap(), 1.0e300)},
                                                   {vztest::temperatureBc(1, study.startCap(), 300.0)}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "overflow");
    const CliRun validate = invoke({"thermal-validate", cliPath(file), "--json"});
    REQUIRE(validate.exitCode == kFailed);
    const Json j = parseObject(validate.out);
    CHECK_FALSE(j["accepted"].boolean());
    CHECK(j["stoppedAt"].text() == "solve");
    bool refusal = false;
    for (std::size_t i = 0; i < j["validation"]["findings"].size(); ++i) {
        refusal = refusal || j["validation"]["findings"][i]["code"].text() == "solve_failed";
    }
    CHECK(refusal);
    requireRefused(invoke({"thermal-solve", cliPath(file), "--json"}), "solve_failed");
}

TEST_CASE("ThermalCli_ValidationJsonWritesANonFiniteMeasurementAsNullAndSaysSo", "[cli][thermal][stdout][nonfinite]") {
    thermal::ThermalValidationReport report;
    report.checks.push_back(thermal::ThermalCheck{.rule = thermal::ValidationRule::SolverResidual, .state = thermal::CheckState::Evaluated});
    thermal::ValidationFinding finding;
    finding.code = ValidationCode::ResidualNotFinite;
    finding.severity = thermal::ValidationSeverity::Error;
    finding.rule = thermal::ValidationRule::SolverResidual;
    finding.value = std::nan("");
    finding.threshold = 1.0e-9;
    finding.message = "a residual is not finite";
    report.findings.push_back(finding);
    thermal::ValidationFinding finite = finding;
    finite.code = ValidationCode::ResidualAboveTolerance;
    finite.value = 2.5e-3;
    report.findings.push_back(finite);

    cli::FiniteJson fin;
    const std::string text = cli::validationJson(fin, report).render();
    CHECK(fin.sawNonFinite());
    const Json j = parseObject(text);
    REQUIRE(j["findings"].size() == 2U);
    CHECK(j["findings"][0]["value"].isNull());
    CHECK_FALSE(j["findings"][0]["valueFinite"].boolean());
    CHECK(j["findings"][0]["threshold"].number() == 1.0e-9);
    CHECK(j["findings"][1]["value"].number() == 2.5e-3);
    CHECK(j["findings"][1]["valueFinite"].boolean());
    CHECK(j["status"].text() == "FAIL");
}

TEST_CASE("ThermalCli_ExtremumTieBreakIsTheLowestIdWhateverTheOrder", "[cli][thermal][summary][tie]") {
    // The numbering is ascending, so a real run cannot tell the tie-break from first-wins. Here the ids are NOT in order.
    const std::vector<double> equal{4.0, 4.0, 4.0};
    const std::vector<std::uint64_t> ids{9, 3, 5};
    const auto all = cli::extremaOf(equal.size(), [&](std::size_t i) { return equal[i]; }, [&](std::size_t i) { return ids[i]; });
    REQUIRE(all.has_value());
    CHECK(all->low.id == 3U);
    CHECK(all->high.id == 3U);
    CHECK(all->count == 3U);

    const std::vector<double> values{1.0, 1.0, 2.0, 2.0, 0.5, 0.5, 3.0, 3.0};
    const std::vector<std::uint64_t> elements{8, 4, 7, 6, 12, 11, 20, 15};
    const auto mixed = cli::extremaOf(values.size(), [&](std::size_t i) { return values[i]; },
                                      [&](std::size_t i) { return elements[i]; });
    REQUIRE(mixed.has_value());
    CHECK(mixed->low.value == 0.5);
    CHECK(mixed->low.id == 11U);
    CHECK(mixed->high.value == 3.0);
    CHECK(mixed->high.id == 15U);

    // The first element seeds both ends (never zero): an all-negative field has a negative maximum.
    const std::vector<double> negative{-5.0, -3.0, -9.0};
    const std::vector<std::uint64_t> negativeIds{1, 2, 3};
    const auto below = cli::extremaOf(negative.size(), [&](std::size_t i) { return negative[i]; },
                                      [&](std::size_t i) { return negativeIds[i]; });
    REQUIRE(below.has_value());
    CHECK(below->high.value == -3.0);
    CHECK(below->low.value == -9.0);
    CHECK_FALSE(cli::extremaOf(0, [](std::size_t) { return 0.0; }, [](std::size_t) { return std::uint64_t{0}; }).has_value());
}
TEST_CASE("ThermalCli_SuccessExitCode", "[cli][thermal][exit]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "exit-ok");
    CHECK(static_cast<int>(invoke({"thermal-solve", cliPath(file)}).exitCode) == 0);
    CHECK(static_cast<int>(invoke({"thermal-load-add", cliPath(file), "9", "--generation", "1"}).exitCode) == 0);
}

TEST_CASE("ThermalCli_SyntaxFailureExitCode", "[cli][thermal][exit]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "exit-usage");
    for (const char* command : {"thermal-settings", "thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient",
                                "thermal-heat-flux", "thermal-balance"}) {
        INFO(command);
        requireUsage(invoke({command}));                                  // no document
        requireUsage(invoke({command, cliPath(file), cliPath(file)}));    // two documents
        requireUsage(invoke({command, cliPath(file), "--format", "json"}));   // an option that does not exist
        requireUsage(invoke({command, cliPath(file), "--force"}));            // and no flag bypasses validation
    }
    CHECK(static_cast<int>(invoke({"thermal-solve"}).exitCode) == 2);
}

TEST_CASE("ThermalCli_DocumentFailureExitCode", "[cli][thermal][exit]") {
    const TempDir temp;
    const auto garbage = temp.path() / "garbage.bcad";
    writeFile(garbage, "this is not a document");
    const CliRun run = invoke({"thermal-solve", cliPath(garbage), "--json"});
    CHECK(static_cast<int>(run.exitCode) == 1);
    CHECK(run.out.empty());
    CHECK_THAT(run.err, ContainsSubstring("document_not_loaded"));
    CHECK(static_cast<int>(invoke({"thermal-solve", cliPath(temp.path() / "nope.bcad")}).exitCode) == 1);
}

TEST_CASE("ThermalCli_AnalysisFailureExitCode", "[cli][thermal][exit]") {
    VizStudy study;
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "exit-analysis");
    CHECK(static_cast<int>(invoke({"thermal-solve", cliPath(file)}).exitCode) == 1);
    CHECK(static_cast<int>(invoke({"thermal-validate", cliPath(file)}).exitCode) == 1);
    CHECK(static_cast<int>(invoke({"thermal-balance", cliPath(file)}).exitCode) == 1);
}

TEST_CASE("ThermalCli_NoFlagBypassesValidation", "[cli][thermal][exit][policy]") {
    VizStudy study;
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}));
    const TempDir temp;
    const auto file = saveStudy(study, temp, "no-bypass");
    for (const char* flag : {"--force", "--ignore-validation", "--ignore-stale", "--auto-pin", "--regularize", "--default-material",
                             "--skip-validation", "--no-validate", "--tolerance"}) {
        INFO(flag);
        requireUsage(invoke({"thermal-solve", cliPath(file), flag}));
    }
    // There is no thermal verb that takes a solver tolerance, a default material, a time step or an ambient of its own.
    const std::string help = invoke({"help"}).out;
    // The thermal verbs are two contiguous blocks of the help: the reports at the end of the command list, and the edits at the end
    // of the edit list. (Other commands legitimately take --tolerance, so the search is scoped to these.)
    const std::size_t reportsAt = help.find("thermal-settings");
    const std::size_t reportsEnd = help.find("\nAssembly edits", reportsAt);
    const std::size_t editsAt = help.find("thermal-analysis-add");
    const std::size_t editsEnd = help.find("\nOptions:", editsAt);
    REQUIRE(reportsAt != std::string::npos);
    REQUIRE(reportsEnd != std::string::npos);
    REQUIRE(editsAt != std::string::npos);
    REQUIRE(editsEnd != std::string::npos);
    const std::string thermalHelp = help.substr(reportsAt, reportsEnd - reportsAt) + help.substr(editsAt, editsEnd - editsAt);
    for (const char* absent : {"--tolerance", "--default", "--regularize", "--auto-pin", "--force", "--ignore"}) {
        INFO(absent);
        CHECK(thermalHelp.find(absent) == std::string::npos);
    }
}

TEST_CASE("ThermalCli_HelpDescribesOnlyWhatIsSupported", "[cli][thermal][help]") {
    const std::string help = invoke({"help"}).out;
    for (const char* verb : {"thermal-settings", "thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient",
                             "thermal-heat-flux", "thermal-balance", "thermal-analysis-add", "thermal-load-add", "thermal-load-remove",
                             "thermal-boundary-add", "thermal-boundary-remove"}) {
        INFO(verb);
        CHECK_THAT(help, ContainsSubstring(verb));
    }
    // It claims no physics the core does not have.
    const std::size_t from = help.find("thermal-settings");
    REQUIRE(from != std::string::npos);
    const std::string thermalHelp = help.substr(from);
    for (const char* unsupported : {"radiation", "anisotropic", "temperature-dependent", "contact", "emissivity", "Celsius is"}) {
        INFO(unsupported);
        CHECK(thermalHelp.find(unsupported) == std::string::npos);
    }
    CHECK_THAT(help, ContainsSubstring("there is no Celsius"));
}

TEST_CASE("ThermalCli_ThermalEditsAreBatchVerbs", "[cli][thermal][edit][batch]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "batch");
    const auto script = temp.path() / "script.txt";
    writeFile(script,
              "# one transaction\n"
              "thermal-analysis-add\n"
              "thermal-load-add 1 --generation 1e5\n"
              "thermal-boundary-add 1 face:Extrude001:start_cap --temperature 300\n"
              "thermal-boundary-add 2 face:Extrude001:end_cap --convection 25 --ambient 293.15\n");
    const CliRun ok = invoke({"batch", cliPath(file), cliPath(script)});
    REQUIRE(ok.exitCode == kOk);
    CHECK(invoke({"thermal-solve", cliPath(file)}).exitCode == kOk);

    // A failing line leaves the file exactly as the successful batch left it: all of the script or none.
    const std::string before = bytesOf(file);
    writeFile(script, "thermal-load-add 5 --generation 7\nthermal-load-add 1 --generation 9\n");
    const CliRun failed = invoke({"batch", cliPath(file), cliPath(script)});
    CHECK(failed.exitCode != kOk);
    CHECK(bytesOf(file) == before);
}

// ===========================================================================
// What the independent review found
// ===========================================================================

TEST_CASE("ThermalCli_AFinalOnlyTransientStartsWhereTheGridStartsNotWhereItWasStored", "[cli][thermal][transient][time]") {
    VizStudy study;
    // ONE step with final-only output: the only STORED state is the last one, so the first stored time is the end time; the analysis
    // itself starts at the grid's first time. The summary names the two apart.
    transientStudy(study, 5.0, 5.0, thermal::TransientOutputPolicy::FinalOnly);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "final-only-solve");
    const Json j = parseObject(invoke({"thermal-solve", cliPath(file), "--json"}).out);
    CHECK(j["stepCount"].number() == 1.0);
    CHECK(j["storedStateCount"].number() == 1.0);
    CHECK(j["startTime"].quantity("s") == 0.0);
    CHECK(j["endTime"].quantity("s") == 5.0);
    CHECK(j["firstStoredTime"].quantity("s") == 5.0);
    CHECK(j["lastStoredTime"].quantity("s") == 5.0);
    // An every-step run stores one more state than it takes steps, and its first stored time is the start.
    VizStudy every;
    transientStudy(every, 0.5, 2.0);
    const auto everyFile = saveStudy(every, temp, "every-step-solve");
    const Json e = parseObject(invoke({"thermal-solve", cliPath(everyFile), "--json"}).out);
    CHECK(e["stepCount"].number() == 4.0);
    CHECK(e["storedStateCount"].number() == 5.0);
    CHECK(e["firstStoredTime"].quantity("s") == 0.0);
    CHECK(e["startTime"].quantity("s") == 0.0);
}

TEST_CASE("ThermalCli_ARejectedBalanceIsStillPrintedAndTheExitStatusSaysNo", "[cli][thermal][summary][balance][exit]") {
    // A conductivity of 1e-300 W/(m K): the chain completes, and the validator does not accept the result. The balance EXISTS, so it
    // is printed (the terms that disagree are what an engineer needs) -- and the exit status is still the verdict's.
    const TempDir temp;
    std::string text = bytesOf(exampleFile("thermal_plate.bcad"));
    const std::string needle = "\"value\": 45.0";
    REQUIRE(text.find(needle) != std::string::npos);
    text.replace(text.find(needle), needle.size(), "\"value\": 1e-300");
    const auto file = temp.path() / "k-tiny.bcad";
    writeFile(file, text);

    const CliRun balance = invoke({"thermal-balance", cliPath(file), "--json"});
    INFO(balance.err);
    CHECK(balance.exitCode == kFailed);
    REQUIRE_FALSE(balance.out.empty());
    const Json j = parseObject(balance.out);
    CHECK_FALSE(j["accepted"].boolean());
    CHECK_FALSE(j["balanced"].boolean());
    CHECK(j["steps"].size() == 19U);   // no step is omitted
    const CliRun validate = invoke({"thermal-validate", cliPath(file), "--json"});
    CHECK(validate.exitCode == kFailed);
    CHECK_FALSE(parseObject(validate.out)["accepted"].boolean());
    // And the commands whose payload is a RESULT print nothing for it.
    for (const char* command : {"thermal-solve", "thermal-temperature", "thermal-gradient", "thermal-heat-flux"}) {
        INFO(command);
        const CliRun refused = invoke({command, cliPath(file), "--json"});
        CHECK(refused.exitCode == kFailed);
        CHECK(refused.out.empty());
    }
}

TEST_CASE("ThermalCli_ACommandLineFaultHasOneExitCodeWhateverTheDocumentHolds", "[cli][thermal][exit][usage]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto withAnalysis = saveStudy(study, temp, "has-analysis");
    const auto without = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "has-none");
    const std::string face = faceText("end_cap");
    for (const auto& file : {withAnalysis, without}) {
        INFO(cliPath(file));
        requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "5"}));                       // no face
        requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "5", "--generation", "5"}));   // two kinds
        requireUsage(invoke({"thermal-load-add", cliPath(file), "5", "--flux", "nan", "--face", face}));
        requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", face, "--convection", "10"}));      // no ambient
        requireUsage(invoke({"thermal-boundary-add", cliPath(file), "5", "face:Nowhere:end_cap", "--temperature", "nan"}));
        requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--mode", "bogus"}));
        requireUsage(invoke({"thermal-analysis-add", cliPath(file), "--output", "sometimes"}));
        requireUsage(invoke({"thermal-load-remove", cliPath(file), "x"}));
    }
}

TEST_CASE("ThermalCli_AnIdAboveTheFileCapIsACommandLineFaultNotASaveFailure", "[cli][thermal][edit][id][batch]") {
    VizStudy study;
    steadyStudy(study);
    const TempDir temp;
    const auto file = saveStudy(study, temp, "big-id");
    const std::string before = bytesOf(file);
    // 2^53 - 1 is the largest identifier a document file can hold; one more would pass every check and fail at save.
    requireUsage(invoke({"thermal-load-add", cliPath(file), "9007199254740992", "--generation", "5"}));
    requireUsage(invoke({"thermal-load-add", cliPath(file), "999999999999999999", "--generation", "5"}));
    requireUsage(invoke({"thermal-load-remove", cliPath(file), "9007199254740992"}));
    CHECK(bytesOf(file) == before);
    // The largest one saves, and a --dry-run agrees with the real run about a script that uses it.
    CHECK(invoke({"thermal-load-add", cliPath(file), "9007199254740991", "--generation", "5"}).exitCode == kOk);

    const auto script = temp.path() / "script.txt";
    writeFile(script, "thermal-load-add 999999999999999999 --generation 5\n");
    const auto other = saveStudy(study, temp, "big-id-batch");
    const std::string untouched = bytesOf(other);
    const CliRun dry = invoke({"batch", cliPath(other), cliPath(script), "--dry-run"});
    const CliRun real = invoke({"batch", cliPath(other), cliPath(script)});
    CHECK(dry.exitCode == real.exitCode);
    CHECK(real.exitCode != kOk);
    CHECK(bytesOf(other) == untouched);
}

TEST_CASE("ThermalCli_TheOutputPolicyAcceptsWhatSettingsPrintsAndANameClashHasItsOwnCode", "[cli][thermal][edit][analysis]") {
    const TempDir temp;
    for (const char* spelling : {"every-step", "every_step"}) {
        INFO(spelling);
        const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, std::format("spelling-{}", spelling));
        REQUIRE(invoke({"thermal-analysis-add", cliPath(file), "--mode", "transient", "--initial-temperature", "300", "--time-step",
                        "0.5", "--end-time", "2", "--output", spelling})
                    .exitCode == kOk);
        CHECK(parseObject(invoke({"thermal-settings", cliPath(file), "--json"}).out)["transient"]["output"].text() == "every_step");
    }
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "name-clash");
    // "Sketch001" is the name of another object in the document.
    requireRefused(invoke({"thermal-analysis-add", cliPath(file), "--name", "Sketch001"}), "duplicate_name");
}

TEST_CASE("ThermalCli_NoMeshingControlIsRefusedByCode", "[cli][thermal][failure][mesh]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("plate.bcad"), temp, "no-control");
    const std::string before = bytesOf(file);
    requireRefused(invoke({"thermal-analysis-add", cliPath(file)}), "no_mesh_control");
    CHECK(bytesOf(file) == before);
}

TEST_CASE("ThermalCli_AModelThatWillNotRegenerateIsTheCliAnalogueOfStaleGeometry", "[cli][thermal][failure][geometry]") {
    // The thickness parameter is zero, so the extrusion cannot be built. Geometry that cannot be regenerated is never handed to the
    // mesher or the analysis: every command that needs the model refuses it, by a code, and the file is untouched.
    const TempDir temp;
    std::string text = bytesOf(exampleFile("thermal_plate.bcad"));
    const std::string needle = "\"si_value\": 0.02,";
    REQUIRE(text.find(needle) != std::string::npos);
    text.replace(text.find(needle), needle.size(), "\"si_value\": 0.0,");
    const auto file = temp.path() / "no-geometry.bcad";
    writeFile(file, text);
    const std::string before = bytesOf(file);
    for (const char* command : {"thermal-settings", "thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient",
                                "thermal-heat-flux", "thermal-balance"}) {
        INFO(command);
        const CliRun run = invoke({command, cliPath(file), "--json"});
        requireRefused(run, "regeneration_failed");
        CHECK_THAT(run.err, ContainsSubstring("extrude depth must be positive"));
    }
    CHECK(bytesOf(file) == before);
}

TEST_CASE("ThermalCli_ThirdPartyOutputDoesNotPolluteStdout", "[cli][thermal][stdout][backend]") {
    // A local size restriction makes Netgen write a warning to the C++ standard output stream. It is contained in the meshing adapter;
    // here std::cout's buffer is swapped for a capture around every thermal command that meshes, and the capture must stay EMPTY while
    // the payload goes only to the stream the driver was given. (The real-process half is the workflow.)
    VizStudy study(std::nullopt, Length::fromSi(0.005));
    steadyStudy(study);
    // The restriction is LIVE: the refined block has more nodes than the unrefined one, so the backend really was asked to apply it.
    const VizStudy plain;
    REQUIRE(study.mesh().nodes().size() > plain.mesh().nodes().size());
    const TempDir temp;
    const auto file = saveStudy(study, temp, "local-size");
    for (const char* command : {"thermal-validate", "thermal-solve", "thermal-temperature", "thermal-gradient", "thermal-heat-flux",
                                "thermal-balance"}) {
        INFO(command);
        std::ostringstream captured;
        std::streambuf* const saved = std::cout.rdbuf(captured.rdbuf());
        CliRun run;
        try {
            run = invoke({command, cliPath(file), "--json"});
        } catch (...) {
            std::cout.rdbuf(saved);
            throw;
        }
        std::cout.rdbuf(saved);
        CHECK(captured.str().empty());
        REQUIRE(run.exitCode == kOk);
        (void)parseObject(run.out);
    }
}

TEST_CASE("ThermalCli_HelpPrintsEveryReportUsageVerbatim", "[cli][thermal][help]") {
    const std::string help = invoke({"help"}).out;
    for (const std::string_view usage :
         {cli::kThermalSettingsUsage, cli::kThermalValidateUsage, cli::kThermalSolveUsage, cli::kThermalTemperatureUsage,
          cli::kThermalGradientUsage, cli::kThermalHeatFluxUsage, cli::kThermalBalanceUsage}) {
        INFO(usage);
        CHECK(help.find(std::format("  {}\n", usage)) != std::string::npos);
    }
    // And the usage of an edit names the units the parser accepts, not spellings it rejects.
    CHECK_THAT(help, ContainsSubstring("--flux <W/m^2>"));
    CHECK_THAT(help, ContainsSubstring("--convection <W/(m^2 K)>"));
    CHECK(help.find("<W/m2>") == std::string::npos);
    CHECK(help.find("<W/(m2 K)>") == std::string::npos);
}

TEST_CASE("ThermalCli_ABatchWithAnUnreadableLineWritesNothing", "[cli][thermal][edit][batch]") {
    const TempDir temp;
    const auto file = copyOf(exampleFile("thermal_bare_plate.bcad"), temp, "batch-usage");
    const std::string before = bytesOf(file);
    const auto script = temp.path() / "script.txt";
    writeFile(script, "thermal-analysis-add\nthermal-load-add 1 --flux 5\n");   // the second line has no face
    const CliRun run = invoke({"batch", cliPath(file), cliPath(script)});
    CHECK(run.exitCode != kOk);
    CHECK(bytesOf(file) == before);
}

TEST_CASE("ThermalCliWorkflow_EveryPayloadIsStrictJson", "[.][workflow]") {
    // Run BY ThermalWorkflow.cmake (hidden from the default run, which has no workflow directory), against the raw bytes the real
    // processes wrote. CMake's string(JSON) accepts trailing text, duplicate keys and a leading zero, so the strictness the workflow
    // cannot give itself comes from the same strict reader the in-process tests use.
    const char* const directory = std::getenv("BETTERCAD_THERMAL_WORKFLOW_DIR");
    REQUIRE(directory != nullptr);
    std::size_t parsed = 0;
    std::size_t empty = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path{directory} / "payloads")) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        INFO(entry.path().string());
        const std::string bytes = readFile(entry.path());
        if (bytes.empty()) {
            ++empty;   // a command that could not answer prints nothing, which the workflow asserts separately
            continue;
        }
        std::string why;
        const std::optional<Json> json = JsonReader::parse(bytes, &why);
        INFO(why);
        REQUIRE(json.has_value());
        CHECK(json->isObject());
        CHECK(bytes.front() == '{');
        CHECK((bytes.ends_with("}\n") || bytes.ends_with("}\r\n")));
        CHECK(bytes.find('\x1b') == std::string::npos);
        ++parsed;
    }
    CHECK(parsed >= 25U);   // zero-match protection: a workflow that wrote nothing must not pass (27 payloads are written today)
    CHECK(empty >= 10U);    // and the refusals, which print nothing, are there too (13 today)
}