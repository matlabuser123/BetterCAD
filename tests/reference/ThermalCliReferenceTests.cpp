// The thermal reference models through the REAL command line (P18-REFMOD-001: "CLI validation").
//
// A HIDDEN test, run BY tests/cli/ThermalReferenceWorkflow.cmake after it has driven the real `bettercad-cli` binary over the
// documents the real `bettercad_example_reference_models` binary wrote. Nothing here runs the CLI: it reads the RAW BYTES each
// process wrote, parses them with the strict RFC 8259 reader (MiniJson.hpp), and compares the NUMBERS
//
//   1. with the CLOSED FORMS of the oracle header (no BetterCAD code is asked what the answer should be), and
//   2. with the same model run IN PROCESS through the core,
//
// so a CLI that printed a wrong digit, a wrong unit, a wrong sign or somebody else's number would fail here even though every
// one of its exit codes was right.

#include "reference/ThermalReferenceSupport.hpp"
#include "cli/MiniJson.hpp"
#include "support/TestFiles.hpp"

#include <cstdlib>
#include <filesystem>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::thermalref;

[[nodiscard]] std::filesystem::path payloadsDirectory() {
    const char* const directory = std::getenv("BETTERCAD_REFMOD_CLI_DIR");
    REQUIRE(directory != nullptr);
    return std::filesystem::path{directory} / "payloads";
}

/// The strictly parsed payload `<name>.json`; the test fails if it is missing or not exactly one JSON object.
[[nodiscard]] Json payload(const std::string& name) {
    const std::string bytes = readFile(payloadsDirectory() / (name + ".json"));
    REQUIRE_FALSE(bytes.empty());
    std::string why;
    const std::optional<Json> json = JsonReader::parse(bytes, &why);
    INFO(name << ": " << why);
    REQUIRE(json.has_value());
    REQUIRE(json->isObject());
    CHECK(bytes.front() == '{');
    CHECK((bytes.ends_with("}\n") || bytes.ends_with("}\r\n")));
    return *json;
}

/// What the same model prints when run in process.
struct InProcess {
    double minTemperature = 0.0;
    double maxTemperature = 0.0;
    double maxFlux = 0.0;
    double worstResidual = 0.0;
};

[[nodiscard]] InProcess inProcess(Result<reference::ThermalBlockModel> built) {
    REQUIRE(built.has_value());
    Run run(std::move(built->document));
    REQUIRE(run.run().accepted());
    InProcess out{1e300, -1e300, 0.0, run.balance().worstNormalizedResidual()};
    for (const Temperature& t : run.post().finalState().temperatures) {
        out.minTemperature = std::min(out.minTemperature, t.si());
        out.maxTemperature = std::max(out.maxTemperature, t.si());
    }
    for (const HeatFlux& q : run.post().finalState().fields.heatFluxMagnitudes) {
        out.maxFlux = std::max(out.maxFlux, q.si());
    }
    return out;
}

constexpr Tolerance kPrinted{1e-9, 1e-9}; // a number printed with 17 digits and read back: far tighter than any physics here

} // namespace

TEST_CASE("ThermalRefmodCli_SteadySlabAgreesWithFourier", "[.][refmod-cli]") {
    const double k = reference::kSlabMaterial.conductivity;
    const heat::Slab slab{.conductivity = k, .area = 0.25, .length = 1.0, .hot = reference::kSlabHotKelvin, .cold = reference::kSlabColdKelvin};
    const Json solve = payload("rm02.solve");
    requireClose(solve["temperature"]["min"]["value"].number(), reference::kSlabColdKelvin, kPrinted, "CLI min temperature");
    requireClose(solve["temperature"]["max"]["value"].number(), reference::kSlabHotKelvin, kPrinted, "CLI max temperature");
    CHECK(solve["temperature"]["unit"].text() == "K");
    CHECK(solve["validation"]["accepted"].boolean());
    CHECK(solve["heatBalance"]["balanced"].boolean());

    const Json flux = payload("rm02.heatflux");
    CHECK(flux["unit"].text() == "W/m^2");
    requireClose(flux["magnitude"]["max"].number(), slab.flux(), kPrinted, "CLI largest heat flux, k dT / L");
    requireClose(flux["magnitude"]["min"].number(), slab.flux(), kPrinted, "CLI smallest heat flux (uniform field)");
    // the direction: the flux runs from the hot cap at z = 0 toward +z, along a uniform field
    requireClose(flux["largest"]["flux"]["z"].number(), slab.flux(), kPrinted, "CLI flux z component");
    requireClose(flux["largest"]["flux"]["x"].number(), 0.0, Tolerance{1e-8, 0.0}, "CLI flux x component");
    requireClose(flux["largest"]["flux"]["y"].number(), 0.0, Tolerance{1e-8, 0.0}, "CLI flux y component");

    const Json balance = payload("rm02.balance");
    CHECK(balance["balanced"].boolean());
    // the heat the held faces exchange: +Qdot in at the hot cap and -Qdot at the cold one, so the sum of reactions is ZERO
    requireClose(balance["final"]["dirichletReactionInto"]["value"].number(), 0.0, Tolerance{1e-9, 0.0}, "CLI net reaction");

    // and the same model in process
    const InProcess core = inProcess(reference::buildThermalSlabReferenceModel());
    requireClose(solve["temperature"]["min"]["value"].number(), core.minTemperature, kPrinted, "CLI vs core, min T");
    requireClose(solve["temperature"]["max"]["value"].number(), core.maxTemperature, kPrinted, "CLI vs core, max T");
    requireClose(flux["magnitude"]["max"].number(), core.maxFlux, kPrinted, "CLI vs core, max flux");
    requireClose(balance["worstNormalizedResidual"].number(), core.worstResidual, Tolerance{1e-12, 0.0}, "CLI vs core, balance residual");
}

TEST_CASE("ThermalRefmodCli_ConvectionAgreesWithSeriesResistance", "[.][refmod-cli]") {
    const heat::ConvectionSlab model{.conductivity = reference::kConvectionMaterial.conductivity,
                                     .area = 0.4 * 0.5,
                                     .length = 0.5,
                                     .coefficient = reference::kConvectionCoefficient,
                                     .hot = reference::kConvectionHotKelvin,
                                     .ambient = reference::kConvectionAmbientKelvin};
    const Json solve = payload("rm05.solve");
    requireClose(solve["temperature"]["max"]["value"].number(), reference::kConvectionHotKelvin, kPrinted, "CLI max temperature (the held cap)");
    requireClose(solve["temperature"]["min"]["value"].number(), model.surfaceTemperature(), kPrinted, "CLI min temperature (the convecting cap)");

    const Json balance = payload("rm05.balance");
    CHECK(balance["balanced"].boolean());
    // SIGN CONVENTION: heat INTO the domain. The held cap supplies +Qdot and the convecting face carries it away, -Qdot.
    requireClose(balance["final"]["dirichletReactionInto"]["value"].number(), model.heatRate(), kPrinted, "CLI reaction at the held cap");
    requireClose(balance["final"]["convectionInto"]["value"].number(), -model.heatRate(), kPrinted, "CLI heat through the convecting face");
    requireClose(balance["convections"][0]["heatInto"]["value"].number(), -model.heatRate(), kPrinted, "CLI per-condition convection heat");
    requireClose(balance["convections"][0]["area"]["value"].number(), 0.4 * 0.5, kPrinted, "CLI convecting area");
    requireClose(balance["final"]["residual"]["value"].number(), 0.0, Tolerance{1e-9, 0.0}, "CLI balance residual (W)");

    const Json flux = payload("rm05.heatflux");
    requireClose(flux["magnitude"]["max"].number(), model.flux(), kPrinted, "CLI heat flux, Qdot / A");

    const InProcess core = inProcess(reference::buildThermalConvectionReferenceModel());
    requireClose(solve["temperature"]["min"]["value"].number(), core.minTemperature, kPrinted, "CLI vs core, min T");
    requireClose(flux["magnitude"]["max"].number(), core.maxFlux, kPrinted, "CLI vs core, max flux");
}

TEST_CASE("ThermalRefmodCli_TransientAgreesWithBackwardEuler", "[.][refmod-cli]") {
    const double side = reference::kLumpedSideMm * 1.0e-3;
    const heat::Lumped model{.conductivity = reference::kLumpedMaterial.conductivity,
                             .density = reference::kLumpedMaterial.density,
                             .specificHeat = reference::kLumpedMaterial.specificHeat,
                             .volume = side * side * side,
                             .area = 6.0 * side * side,
                             .coefficient = reference::kLumpedCoefficient,
                             .initial = reference::kLumpedInitialKelvin,
                             .ambient = reference::kLumpedAmbientKelvin};
    const double expected = model.backwardEuler(46, 100.0);
    const Json temperature = payload("rm08.temperature");
    CHECK(temperature["state"]["index"].number() == 46.0);
    requireClose(temperature["state"]["time"]["value"].number(), 4600.0, kPrinted, "CLI final time");
    // the lumped model has a spatial spread of Bi-order (measured 0.0084 K here); both ends must bracket the oracle within it
    const double low = temperature["temperature"]["min"]["value"].number();
    const double high = temperature["temperature"]["max"]["value"].number();
    CHECK(low <= high);
    requireClose(low, expected, Tolerance{0.02, 0.0}, "CLI coolest node vs backward Euler");
    requireClose(high, expected, Tolerance{0.02, 0.0}, "CLI hottest node vs backward Euler");

    const Json balance = payload("rm08.balance");
    CHECK(balance["balanced"].boolean());
    CHECK(balance["stateCount"].number() == 46.0);
    requireClose(balance["final"]["time"]["value"].number(), 4600.0, kPrinted, "CLI final balance time");
    requireClose(balance["final"]["stepSize"]["value"].number(), 100.0, kPrinted, "CLI final step size");
    // the energy balance of the last step: stored-energy rate = heat in, and the heat in is the convection (negative: cooling)
    requireClose(balance["final"]["storedEnergyRate"]["value"].number(), balance["final"]["convectionInto"]["value"].number(),
                 Tolerance{1e-6, 1e-8}, "CLI stored-energy rate equals the convective heat");

    const Json solve = payload("rm08.solve");
    CHECK(solve["stepCount"].number() == 46.0);
    CHECK(solve["storedStateCount"].number() == 47.0);
    requireClose(solve["endTime"]["value"].number(), 4600.0, kPrinted, "CLI end time");
}

TEST_CASE("ThermalRefmodCli_ExpectedFailuresReportTheirStage", "[.][refmod-cli]") {
    struct Expectation {
        const char* name;
        const char* stage;
    };
    for (const Expectation& e : {Expectation{"rm09.validate", "solve"}, Expectation{"rm09b.validate", "solve"},
                                 Expectation{"rm10load.validate", "loads"}, Expectation{"rm10temperature.validate", "boundary"},
                                 Expectation{"rm10convection.validate", "boundary"}}) {
        INFO(e.name);
        const Json report = payload(e.name);
        CHECK_FALSE(report["accepted"].boolean());
        CHECK(report["stoppedAt"].text() == e.stage);
        CHECK(report["validation"]["status"].text() == "FAIL");
        CHECK(report["validation"]["errorCount"].number() >= 1.0);
    }
    // each names its OWN cause
    const auto firstError = [](const Json& report) {
        const Json& findings = report["validation"]["findings"];
        for (std::size_t i = 0; i < findings.size(); ++i) {
            if (findings[i]["severity"].text() == "error") {
                return findings[i]["code"].text();
            }
        }
        return std::string{};
    };
    CHECK(firstError(payload("rm09.validate")) == "no_temperature_datum");
    CHECK(firstError(payload("rm09b.validate")) == "no_temperature_datum");
    CHECK(firstError(payload("rm10load.validate")) == "loads_invalid");
    CHECK(firstError(payload("rm10temperature.validate")) == "boundary_conditions_invalid");
    CHECK(firstError(payload("rm10convection.validate")) == "boundary_conditions_invalid");
}

TEST_CASE("ThermalRefmodCli_SurfaceFluxAndTheHeldCapTransientAgree", "[.][refmod-cli]") {
    // RM-THERM-02 surface flux: +500 W/m^2 INTO a 50 x 50 x 100 mm slab (k 5) whose start cap is held at 300 K.
    //   Qdot = 500 x 0.0025 = 1.25 W; dT/dz = +100 K/m; T(end) = 310 K; flux = -500 W/m^2 along z (toward the held cap).
    const Json solve = payload("rm02b.solve");
    requireClose(solve["temperature"]["min"]["value"].number(), 300.0, kPrinted, "CLI min temperature");
    requireClose(solve["temperature"]["max"]["value"].number(), 310.0, kPrinted, "CLI max temperature (the end cap)");
    const Json flux = payload("rm02b.heatflux");
    requireClose(flux["magnitude"]["max"].number(), 500.0, kPrinted, "CLI largest heat flux");
    requireClose(flux["magnitude"]["min"].number(), 500.0, kPrinted, "CLI smallest heat flux");
    requireClose(flux["largest"]["flux"]["z"].number(), -500.0, kPrinted, "CLI flux z component (toward the held cap)");
    const Json balance = payload("rm02b.balance");
    CHECK(balance["balanced"].boolean());
    requireClose(balance["final"]["surfaceHeatInto"]["value"].number(), +1.25, kPrinted, "CLI surface heat INTO the body");
    requireClose(balance["final"]["dirichletReactionInto"]["value"].number(), -1.25, kPrinted, "CLI held-cap reaction");
    const InProcess flat = inProcess(reference::buildThermalSurfaceFluxReferenceModel());
    requireClose(solve["temperature"]["max"]["value"].number(), flat.maxTemperature, kPrinted, "CLI vs core, max T");

    // RM-THERM-08 held-cap transient: the CLI's final state equals the core's, and it is a real transient (ten steps, 500 s).
    const Json temperature = payload("rm08b.temperature");
    CHECK(temperature["state"]["index"].number() == 10.0);
    requireClose(temperature["state"]["time"]["value"].number(), 500.0, kPrinted, "CLI final time");
    requireClose(temperature["temperature"]["max"]["value"].number(), reference::kTransientSlabHeldKelvin, kPrinted, "CLI hottest node (the held cap)");
    const InProcess core = inProcess(reference::buildThermalTransientSlabReferenceModel());
    requireClose(temperature["temperature"]["min"]["value"].number(), core.minTemperature, kPrinted, "CLI vs core, min T");
    requireClose(temperature["temperature"]["max"]["value"].number(), core.maxTemperature, kPrinted, "CLI vs core, max T");
    CHECK(temperature["temperature"]["min"]["value"].number() > reference::kTransientSlabInitialKelvin);
    const Json transientBalance = payload("rm08b.balance");
    CHECK(transientBalance["balanced"].boolean());
    CHECK(transientBalance["stateCount"].number() == 10.0);
}
TEST_CASE("ThermalRefmodCli_EditedModelGivesTheNewAnswer", "[.][refmod-cli]") {
    // The stale-mesh reference model, edited in the SAVED INTENT between two processes (30 mm -> 36 mm): the second process
    // regenerates and remeshes, so it reports the NEW flux k dT / L. No process remembers another's mesh or result.
    const double k = 5.0;
    const Json before = payload("rm10stale.before.heatflux");
    const Json after = payload("rm10stale.after.heatflux");
    requireClose(before["magnitude"]["max"].number(), k * 100.0 / 0.030, kPrinted, "the flux of the 30 mm model, k dT / L");
    requireClose(after["magnitude"]["max"].number(), k * 100.0 / 0.036, kPrinted, "the flux of the 36 mm model, k dT / L");
    CHECK(before["magnitude"]["max"].number() != after["magnitude"]["max"].number());
}

TEST_CASE("ThermalRefmodCli_EveryPayloadIsStrictJson", "[.][refmod-cli-strict]") {
    // Every non-empty .json payload the workflow wrote is exactly one RFC 8259 object followed by a line ending (CMake's string(JSON)
    // accepts trailing text and repeated keys; this reader does not). The refusals print nothing, which the workflow asserts.
    std::size_t parsed = 0;
    std::size_t empty = 0;
    for (const auto& entry : std::filesystem::directory_iterator(payloadsDirectory())) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        INFO(entry.path().string());
        const std::string bytes = readFile(entry.path());
        if (bytes.empty()) {
            ++empty;
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
    CHECK(parsed == 32U); // zero-match protection, exact: 5 models x (validate, solve, re-solve) + 10 summaries + 5 refusal reports + 2 stale = 32
    CHECK(empty == 25U);  // the 25 refused summaries print nothing
}