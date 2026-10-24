// P18-PERSIST-001: persisting canonical thermal-analysis intent.
//
// THE RULE THESE TESTS EXIST FOR:
//
//     save  ->  thermal-analysis intent
//     load  ->  restore intent; regenerate the mesh through P16; solve when asked
//
// NOT: save the temperature array and trust it afterwards. So the comparisons here are of
// CANONICAL INTENT -- read through a fingerprint that reads the mesh control, the mode, the
// loads and conditions with their targets and values at full precision, the initial
// condition and both settings records, and reads nothing generated. A fingerprint that
// matched because a cached result happened to match would be no evidence at all.
//
// THE HARD GATE IS BYTE IDENTITY ACROSS THE SOLVE: the same document, saved before and after
// a solve, a recovery, a balance, a validation and a display-state build, must produce the
// same bytes -- for a steady analysis AND for a transient one whose result is two hundred
// states large -- and a failed solve and a stale result must not move a byte either.

#include "TestHelpers.hpp"
#include "renderer/ThermalVizTestSupport.hpp"
#include "support/TestFiles.hpp"
#include "thermal/ThermalCommandTestSupport.hpp"

#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/thermal/ThermalCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using thermal::ConvectionBoundary;
using thermal::PrescribedTemperature;
using thermal::SurfaceHeatFluxLoad;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalAnalysisMode;
using thermal::ThermalBoundaryCondition;
using thermal::ThermalLoad;
using thermal::TransientOutputPolicy;
using thermal::TransientSettings;
using thermal::UniformInitialTemperature;
using thermal::VolumetricHeatSourceLoad;
using vztest::VizStudy;

namespace {

// DELIBERATELY UGLY NUMBERS, and none of them a default. A round value would pass a
// low-precision formatter; these would not. Every one is a distinct decimal, so a swap of
// two fields, a unit rescale (K <-> degC, W/m^2 <-> kW/m^2, s <-> ms) or a default
// substituted for a dropped field shows.
constexpr double kSink = -1234.5;           // W/m^2, a heat-extracting face load
constexpr double kGeneration = 67890.25;    // W/m^3
constexpr double kHeld = 345.125;           // K
constexpr double kCoefficient = 27.75;      // W/(m^2 K)
constexpr double kAmbientK = 291.625;       // K
constexpr double kInitialK = 312.875;       // K
constexpr double kStep = 0.125;             // s
constexpr double kEnd = 2.375;              // s (nineteen whole steps)
constexpr double kTolerance = 1.23456789e-8;
constexpr double kPivotFloor = 2.5e-11;

[[nodiscard]] thermal::ThermalSolverSettings settings() {
    return thermal::ThermalSolverSettings{.relativeResidualTolerance = kTolerance,
                                          .pivotFloor = kPivotFloor};
}

/// The full steady fixture: both load kinds, mixed signs and a zero, both condition kinds, a
/// non-default order (ids 4, 2, 7 and 3, 1), two distinct resolvable faces and non-default
/// settings.
[[nodiscard]] ThermalAnalysisDefinition steadyGolden(const VizStudy& study) {
    ThermalAnalysisDefinition definition;
    definition.mesh = study.control;
    definition.mode = ThermalAnalysisMode::Steady;
    definition.solver = settings();
    definition.loads.push_back(vztest::surfaceLoad(4, study.side(1), kSink));
    definition.loads.push_back(vztest::generationLoad(2, kGeneration));
    definition.loads.push_back(vztest::surfaceLoad(7, study.side(2), 0.0));
    definition.boundaryConditions.push_back(vztest::temperatureBc(3, study.startCap(), kHeld));
    definition.boundaryConditions.push_back(
        vztest::convectionBc(1, study.endCap(), kCoefficient, kAmbientK));
    return definition;
}

[[nodiscard]] ThermalAnalysisDefinition transientGolden(const VizStudy& study) {
    ThermalAnalysisDefinition definition = steadyGolden(study);
    definition.mode = ThermalAnalysisMode::Transient;
    definition.initialCondition = UniformInitialTemperature{Temperature::fromSi(kInitialK)};
    definition.transient = TransientSettings{.timeStep = Time::fromSi(kStep),
                                             .endTime = Time::fromSi(kEnd),
                                             .output = TransientOutputPolicy::EveryStep};
    return definition;
}

[[nodiscard]] std::string describe(const FaceName& name) {
    const auto id = [](const auto& optional) {
        return optional.has_value() ? std::format("{}", optional->value()) : std::string{"-"};
    };
    std::string text = std::format("{},{},{},{},{},{}", name.feature.value(),
                                   static_cast<int>(name.face.role), id(name.face.entity),
                                   id(name.face.along), id(name.face.alongSketch),
                                   id(name.face.edge));
    for (const FaceCopy& copy : name.face.copies) {
        text += std::format(",copy({},{})", copy.feature.value(), copy.instance);
    }
    return text;
}

/// Canonical thermal intent, and nothing else: every field of the definition and no other,
/// read through the public accessors, at seventeen significant digits so that a value that
/// round-tripped to its nearest neighbour would show. No revision, no mesh, no result.
[[nodiscard]] std::string fingerprint(const ThermalAnalysis& analysis) {
    const ThermalAnalysisDefinition& d = analysis.definition();
    std::string text = std::format("mesh={}|mode={}", d.mesh.value(), thermal::toString(d.mode));
    for (const ThermalLoad& load : d.loads) {
        text += std::format("|load[{}:{}]", load.id().value(), thermal::toString(load.kind()));
        if (const SurfaceHeatFluxLoad* surface = load.surfaceHeatFlux()) {
            text += std::format("({})={:.17g}", describe(surface->face), surface->flux.si());
        } else if (const VolumetricHeatSourceLoad* source = load.volumetricHeatSource()) {
            text += std::format("={:.17g}", source->generation.si());
        }
    }
    for (const ThermalBoundaryCondition& condition : d.boundaryConditions) {
        text += std::format("|bc[{}:{}]({})", condition.id().value(),
                            thermal::toString(condition.kind()), describe(condition.face()));
        if (const PrescribedTemperature* held = condition.prescribedTemperature()) {
            text += std::format("={:.17g}", held->value.si());
        } else if (const ConvectionBoundary* convection = condition.convection()) {
            text += std::format("=h{:.17g},T{:.17g}", convection->coefficient.si(),
                                convection->ambient.si());
        }
    }
    text += d.initialCondition.has_value()
                ? std::format("|T0={:.17g}", d.initialCondition->value.si())
                : std::string{"|T0=none"};
    text += std::format("|solver={:.17g},{:.17g}", d.solver.relativeResidualTolerance,
                        d.solver.pivotFloor);
    text += d.transient.has_value()
                ? std::format("|transient={:.17g},{:.17g},{}", d.transient->timeStep.si(),
                              d.transient->endTime.si(), thermal::toString(d.transient->output))
                : std::string{"|transient=none"};
    return text;
}

[[nodiscard]] const ThermalAnalysis& onlyAnalysis(const Document& document) {
    const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(document);
    REQUIRE(ids.size() == 1U);
    const ThermalAnalysis* found = thermal::findThermalAnalysis(document, ids.front());
    REQUIRE(found != nullptr);
    return *found;
}

[[nodiscard]] Document roundTrip(const Document& document, const std::filesystem::path& file) {
    const Result<void> saved = io::saveDocument(document, file);
    INFO((saved ? std::string{} : saved.error().message));
    REQUIRE(saved.has_value());
    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

[[nodiscard]] std::string saveToString(const Document& document) {
    const Result<std::string> text = io::documentToJson(document);
    INFO((text ? std::string{} : text.error().message));
    REQUIRE(text.has_value());
    return *text;
}

constexpr std::string_view kMarker = "\"type\": \"thermal-analysis\"";

/// The thermal-analysis object's own text, from its type marker onward. NEEDED FOR EVERY
/// COMPARISON BETWEEN TWO DOCUMENTS, because a `Document` writes a fresh UUID into its header:
/// two documents never have identical whole-file bytes whatever their intent. The analysis is
/// the last object in every document these tests build, so "onward" is exactly its section.
[[nodiscard]] std::string analysisSectionOf(const std::string& text) {
    const std::size_t at = text.find(kMarker);
    REQUIRE(at != std::string::npos);
    return text.substr(at);
}

/// Replaces the thermal analysis object's `data`, so a malformed field can be tested without
/// hand-writing a whole document.
[[nodiscard]] std::string withAnalysisData(const std::string& json, std::string_view replacement) {
    const std::size_t typeAt = json.find(kMarker);
    REQUIRE(typeAt != std::string::npos);
    const std::size_t dataAt = json.find("\"data\":", typeAt);
    REQUIRE(dataAt != std::string::npos);
    const std::size_t open = json.find('{', dataAt);
    REQUIRE(open != std::string::npos);
    std::size_t depth = 0;
    std::size_t close = open;
    for (; close < json.size(); ++close) {
        if (json[close] == '{') {
            ++depth;
        } else if (json[close] == '}') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
    }
    REQUIRE(close < json.size());
    return json.substr(0, open) + std::string{replacement} + json.substr(close + 1);
}

[[nodiscard]] Error loadFailure(std::string_view json) {
    Result<Document> loaded = io::documentFromJson(json);
    REQUIRE_FALSE(loaded.has_value());
    return loaded.error();
}

constexpr std::string_view kSolver =
    R"("solver": {"relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12})";

[[nodiscard]] std::string faceJson(const VizStudy& study, std::string_view role) {
    return std::format(R"({{"feature": {}, "face": {{"role": "{}"}}}})", study.feature.value(), role);
}

[[nodiscard]] std::string steadyData(const VizStudy& study, std::string_view body) {
    return std::format(R"({{"mesh": {}, "mode": "steady", {}}})", study.control.value(), body);
}

[[nodiscard]] std::string transientData(const VizStudy& study, std::string_view body) {
    return std::format(R"({{"mesh": {}, "mode": "transient", {}}})", study.control.value(), body);
}

/// Puts a LOADED document into a study. A `Regenerator` belongs to one document (it refuses
/// another), and a `Mesher` holds meshes of the old one, so both are replaced; nothing of the
/// old document's derived state is carried over.
void adopt(VizStudy& study, Document&& loaded) {
    study.document = std::move(loaded);
    // The study's own ids belong to the document it built; the loaded one is addressed by its own.
    const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(study.document);
    REQUIRE_FALSE(ids.empty());
    study.analysis = ids.front();
    study.control = thermal::findThermalAnalysis(study.document, ids.front())->definition().mesh;
    study.regenerator = features::Regenerator{};
    study.mesher = meshing::Mesher{};
    requireReport(study.regenerator, study.document);
    study.remesh();
}

/// A transient analysis with a long, fine history, so the result is large.
[[nodiscard]] ThermalAnalysisDefinition longTransient(const VizStudy& study) {
    ThermalAnalysisDefinition definition = transientGolden(study);
    definition.transient = TransientSettings{.timeStep = Time::fromSi(0.01),
                                             .endTime = Time::fromSi(2.0),
                                             .output = TransientOutputPolicy::EveryStep};
    return definition;
}

/// The engineering semantics of one solved state, as plain values, so that nothing of the
/// process that produced it survives into the comparison.
struct Semantics {
    std::vector<double> times;
    std::vector<std::vector<double>> temperatures;
    std::vector<std::array<double, 5>> balance;
    std::optional<thermal::ThermalPostResult> post;
    thermal::ValidationStatus status = thermal::ValidationStatus::Fail;
    std::size_t findings = 0U;
    std::size_t nodes = 0U;
    std::size_t elements = 0U;
};

[[nodiscard]] Semantics semanticsOf(const VizStudy& study) {
    const std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
    INFO(derived->stoppedBecause);
    REQUIRE(derived->post.has_value());
    REQUIRE(derived->balance.has_value());
    Semantics out;
    out.post = *derived->post;
    for (const thermal::ThermalPostState& state : derived->post->states()) {
        out.times.push_back(state.time.has_value() ? state.time->si() : -1.0);
        std::vector<double> kelvin;
        for (const Temperature& t : state.temperatures) {
            kelvin.push_back(t.si());
        }
        out.temperatures.push_back(std::move(kelvin));
    }
    for (const thermal::ThermalHeatBalance& state : derived->balance->states()) {
        out.balance.push_back({state.terms.appliedSurfaceHeat.si(), state.terms.generatedHeat.si(),
                               state.terms.convectionHeat.si(),
                               state.terms.prescribedTemperatureReaction.si(),
                               state.terms.storedEnergyRate.has_value()
                                   ? state.terms.storedEnergyRate->si()
                                   : 0.0});
    }
    const thermal::ThermalValidationReport report = vztest::validationOf(study, *derived);
    out.status = report.status();
    out.findings = report.findings.size();
    out.nodes = study.mesh().nodeCount();
    out.elements = study.mesh().elementCount();
    return out;
}

void requireSame(const Semantics& before, const Semantics& after) {
    CHECK(after.nodes == before.nodes);
    CHECK(after.elements == before.elements);
    CHECK(after.status == before.status);
    CHECK(after.findings == before.findings);
    REQUIRE(after.times.size() == before.times.size());
    CHECK(after.times == before.times);
    REQUIRE(after.temperatures.size() == before.temperatures.size());
    double worst = 0.0;
    for (std::size_t s = 0; s < before.temperatures.size(); ++s) {
        REQUIRE(after.temperatures[s].size() == before.temperatures[s].size());
        for (std::size_t n = 0; n < before.temperatures[s].size(); ++n) {
            worst = std::max(worst, std::abs(after.temperatures[s][n] - before.temperatures[s][n]) /
                                        std::max(1.0, std::abs(before.temperatures[s][n])));
        }
    }
    CHECK(worst <= 1e-12);
    REQUIRE(after.balance.size() == before.balance.size());
    for (std::size_t s = 0; s < before.balance.size(); ++s) {
        for (std::size_t k = 0; k < 5U; ++k) {
            CHECK_THAT(after.balance[s][k],
                       Catch::Matchers::WithinAbs(before.balance[s][k],
                                                  1e-12 * std::max(1.0, std::abs(before.balance[s][k]))));
        }
    }
    // The recovered gradient and heat-flux fields too, state by state.
    REQUIRE(before.post.has_value());
    REQUIRE(after.post.has_value());
    REQUIRE(after.post->stateCount() == before.post->stateCount());
    std::size_t identical = 0U;
    for (std::size_t s = 0; s < before.post->stateCount(); ++s) {
        identical += after.post->states()[s].fields == before.post->states()[s].fields ? 1U : 0U;
    }
    WARN(std::format("worst relative temperature difference {:.3e} over {} states; element fields "
                     "bit-identical in {} of {}",
                     worst, before.temperatures.size(), identical, before.post->stateCount()));
    CHECK(identical == before.post->stateCount());
}

} // namespace

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_TheWholeSteadyCanonicalIntentRoundTrips", "[thermal][io][persist]") {
    TempDir temp;
    VizStudy study;
    vztest::define(study, steadyGolden(study));

    const std::string before = fingerprint(onlyAnalysis(study.document));
    const Document loaded = roundTrip(study.document, temp.path() / "steady.bcad");
    const std::string after = fingerprint(onlyAnalysis(loaded));
    INFO(std::format("before: {}\nafter:  {}", before, after));
    CHECK(before == after);
    // NOT VACUOUS: the fingerprint has to say something, or two empty strings would compare
    // equal over a serializer that wrote nothing.
    CHECK_THAT(before, ContainsSubstring("load[4:surface_heat_flux]"));
    CHECK_THAT(before, ContainsSubstring("load[2:volumetric_heat_source]"));
    CHECK_THAT(before, ContainsSubstring("load[7:surface_heat_flux]"));
    CHECK_THAT(before, ContainsSubstring("bc[3:prescribed_temperature]"));
    CHECK_THAT(before, ContainsSubstring("bc[1:convection]"));
    CHECK_THAT(before, ContainsSubstring("T0=none"));
    // And the whole definition through its own field-complete defaulted operator==, so a member
    // added later without being serialized fails here even if `fingerprint` forgot it.
    CHECK(onlyAnalysis(loaded).definition() == onlyAnalysis(study.document).definition());
}

TEST_CASE("ThermalPersistence_TheWholeTransientCanonicalIntentRoundTrips", "[thermal][io][persist]") {
    TempDir temp;
    VizStudy study;
    vztest::define(study, transientGolden(study));

    const std::string before = fingerprint(onlyAnalysis(study.document));
    const Document loaded = roundTrip(study.document, temp.path() / "transient.bcad");
    CHECK(fingerprint(onlyAnalysis(loaded)) == before);
    CHECK_THAT(before, ContainsSubstring("T0=312.875"));
    CHECK_THAT(before, ContainsSubstring("transient=0.125,2.375,every_step"));
    CHECK(onlyAnalysis(loaded).definition() == onlyAnalysis(study.document).definition());
    CHECK(onlyAnalysis(loaded).definition().initialCondition->value == Temperature::fromSi(kInitialK));
}

TEST_CASE("ThermalPersistence_EveryEnumeratorRoundTrips", "[thermal][io][persist]") {
    // THE GUARD THE KEY TABLES CANNOT BE THEIR OWN. A size check cannot catch a WRONG entry,
    // so every enumerator is put on an analysis in turn and read back.
    TempDir temp;
    SECTION("both modes") {
        VizStudy steady;
        vztest::define(steady, steadyGolden(steady));
        VizStudy transient;
        vztest::define(transient, transientGolden(transient));
        CHECK(onlyAnalysis(roundTrip(steady.document, temp.path() / "m1.bcad")).definition().mode ==
              ThermalAnalysisMode::Steady);
        CHECK(onlyAnalysis(roundTrip(transient.document, temp.path() / "m2.bcad")).definition().mode ==
              ThermalAnalysisMode::Transient);
    }
    SECTION("both output policies") {
        for (const TransientOutputPolicy policy :
             {TransientOutputPolicy::FinalOnly, TransientOutputPolicy::EveryStep}) {
            VizStudy study;
            ThermalAnalysisDefinition definition = transientGolden(study);
            definition.transient->output = policy;
            vztest::define(study, definition);
            const Document loaded = roundTrip(
                study.document, temp.path() / std::format("o{}.bcad", static_cast<int>(policy)));
            INFO(thermal::toString(policy));
            CHECK(onlyAnalysis(loaded).definition().transient->output == policy);
        }
    }
    SECTION("both load kinds and both condition kinds, each alone") {
        VizStudy study;
        ThermalAnalysisDefinition surface = steadyGolden(study);
        surface.loads = {vztest::surfaceLoad(5, study.endCap(), 250.5)};
        surface.boundaryConditions = {vztest::temperatureBc(6, study.startCap(), 301.5)};
        vztest::define(study, surface);
        const Document a = roundTrip(study.document, temp.path() / "k1.bcad");
        REQUIRE(onlyAnalysis(a).definition().loads.size() == 1U);
        CHECK(onlyAnalysis(a).definition().loads[0].kind() == thermal::ThermalLoadKind::SurfaceHeatFlux);
        CHECK(onlyAnalysis(a).definition().boundaryConditions[0].kind() ==
              thermal::ThermalBoundaryConditionKind::PrescribedTemperature);

        ThermalAnalysisDefinition volume = steadyGolden(study);
        volume.loads = {vztest::generationLoad(5, -5000.75)};
        volume.boundaryConditions = {vztest::convectionBc(6, study.startCap(), 12.5, 280.25)};
        vztest::define(study, volume);
        const Document b = roundTrip(study.document, temp.path() / "k2.bcad");
        CHECK(onlyAnalysis(b).definition().loads[0].kind() ==
              thermal::ThermalLoadKind::VolumetricHeatSource);
        CHECK(onlyAnalysis(b).definition().boundaryConditions[0].kind() ==
              thermal::ThermalBoundaryConditionKind::Convection);
    }
}

TEST_CASE("ThermalPersistence_SignedLoadsKeepTheirSignAndTheirZero", "[thermal][io][persist]") {
    // A heat-extracting load is ordinary canonical intent (P18-DATA). A serializer applying
    // abs(), or a reader clamping, passes a positive-only test.
    TempDir temp;
    VizStudy study;
    ThermalAnalysisDefinition definition = steadyGolden(study);
    definition.loads = {vztest::surfaceLoad(1, study.endCap(), 2500.0),
                        vztest::surfaceLoad(2, study.side(1), -2500.0),
                        vztest::surfaceLoad(3, study.side(2), 0.0),
                        vztest::generationLoad(4, 1.0e5),
                        vztest::generationLoad(5, -1.0e5),
                        vztest::generationLoad(6, 0.0)};
    vztest::define(study, definition);
    const Document loaded = roundTrip(study.document, temp.path() / "signs.bcad");
    const auto& after = onlyAnalysis(loaded).definition().loads;
    REQUIRE(after.size() == 6U);
    CHECK(after[0].surfaceHeatFlux()->flux.si() == 2500.0);
    CHECK(after[1].surfaceHeatFlux()->flux.si() == -2500.0);
    CHECK(after[2].surfaceHeatFlux()->flux.si() == 0.0);
    CHECK(after[3].volumetricHeatSource()->generation.si() == 1.0e5);
    CHECK(after[4].volumetricHeatSource()->generation.si() == -1.0e5);
    CHECK(after[5].volumetricHeatSource()->generation.si() == 0.0);
}

TEST_CASE("ThermalPersistence_ExtremeAndAwkwardFiniteValuesSurviveBitForBit",
          "[thermal][io][persist]") {
    // 0.1 + 0.2 is not 0.3; the smallest subnormal, the largest finite double and a negative
    // zero are the values a lossy formatter or a normalising reader changes.
    TempDir temp;
    VizStudy study;
    ThermalAnalysisDefinition definition = steadyGolden(study);
    const double awkward = 0.1 + 0.2;
    const double tiny = 4.9406564584124654e-324;
    definition.loads = {vztest::surfaceLoad(1, study.endCap(), awkward),
                        vztest::generationLoad(2, tiny),
                        vztest::surfaceLoad(3, study.side(1), 1.7976931348623157e308),
                        vztest::surfaceLoad(4, study.side(2), -0.0)};
    definition.boundaryConditions = {vztest::temperatureBc(1, study.startCap(), 0.0),
                                     vztest::convectionBc(2, study.endCap(), tiny, 1.0e-300)};
    definition.solver = {.relativeResidualTolerance = 1.0e-300, .pivotFloor = tiny};
    vztest::define(study, definition);
    const Document loaded = roundTrip(study.document, temp.path() / "awkward.bcad");
    const auto& d = onlyAnalysis(loaded).definition();
    const auto bits = [](double v) { return std::bit_cast<std::uint64_t>(v); };
    CHECK(bits(d.loads[0].surfaceHeatFlux()->flux.si()) == bits(awkward));
    CHECK(bits(d.loads[1].volumetricHeatSource()->generation.si()) == bits(tiny));
    CHECK(bits(d.loads[2].surfaceHeatFlux()->flux.si()) == bits(1.7976931348623157e308));
    // THE SIGN OF ZERO IS KEPT (the defaulted == cannot see it, so the bits are compared).
    CHECK(bits(d.loads[3].surfaceHeatFlux()->flux.si()) == bits(-0.0));
    CHECK(bits(d.boundaryConditions[0].prescribedTemperature()->value.si()) == bits(0.0));
    CHECK(bits(d.boundaryConditions[1].convection()->coefficient.si()) == bits(tiny));
    CHECK(bits(d.boundaryConditions[1].convection()->ambient.si()) == bits(1.0e-300));
    CHECK(bits(d.solver.relativeResidualTolerance) == bits(1.0e-300));
    CHECK(bits(d.solver.pivotFloor) == bits(tiny));
}

TEST_CASE("ThermalPersistence_LoadAndConditionOrderIsTheUsersAndIsPreserved",
          "[thermal][io][persist][determinism]") {
    TempDir temp;
    VizStudy study;
    vztest::define(study, steadyGolden(study));   // loads 4, 2, 7 and conditions 3, 1
    const Document loaded = roundTrip(study.document, temp.path() / "order.bcad");
    const auto& d = onlyAnalysis(loaded).definition();
    REQUIRE(d.loads.size() == 3U);
    CHECK(d.loads[0].id().value() == 4U);
    CHECK(d.loads[1].id().value() == 2U);
    CHECK(d.loads[2].id().value() == 7U);
    REQUIRE(d.boundaryConditions.size() == 2U);
    CHECK(d.boundaryConditions[0].id().value() == 3U);
    CHECK(d.boundaryConditions[1].id().value() == 1U);
}

TEST_CASE("ThermalPersistence_AnEmptyAnalysisAndManyRecordsRoundTrip", "[thermal][io][persist]") {
    TempDir temp;
    VizStudy study;
    SECTION("no loads and no conditions") {
        ThermalAnalysisDefinition definition;
        definition.mesh = study.control;
        definition.solver = settings();
        vztest::define(study, definition);
        const Document loaded = roundTrip(study.document, temp.path() / "empty.bcad");
        CHECK(onlyAnalysis(loaded).definition() == definition);
        const std::string text = saveToString(study.document);
        CHECK(text.find("\"loads\"") == std::string::npos);
        CHECK(text.find("\"boundary_conditions\"") == std::string::npos);
    }
    SECTION("three thousand loads and conditions") {
        ThermalAnalysisDefinition definition;
        definition.mesh = study.control;
        definition.solver = settings();
        for (std::uint64_t i = 1; i <= 3000U; ++i) {
            definition.loads.push_back(vztest::surfaceLoad(i, study.endCap(), static_cast<double>(i) * 0.5));
            definition.boundaryConditions.push_back(
                vztest::convectionBc(i, study.startCap(), 1.0 + static_cast<double>(i), 300.0));
        }
        vztest::define(study, definition);
        const Document loaded = roundTrip(study.document, temp.path() / "many.bcad");
        CHECK(onlyAnalysis(loaded).definition() == definition);
    }
}

TEST_CASE("ThermalPersistence_TwoAnalysesRoundTripWithoutLeaking", "[thermal][io][persist]") {
    // Two analyses with the SAME LoadId and BoundaryConditionId values are two different
    // loads in two different analyses; neither may leak into the other, and each keeps its own
    // object identity.
    TempDir temp;
    VizStudy study;
    ThermalAnalysisDefinition first = steadyGolden(study);
    vztest::define(study, first);
    ThermalAnalysisDefinition second = transientGolden(study);
    second.loads = {vztest::surfaceLoad(4, study.endCap(), 111.25)};
    second.boundaryConditions = {vztest::temperatureBc(3, study.startCap(), 222.5)};
    auto extra = ThermalAnalysis::create("Second", second);
    REQUIRE(extra.has_value());
    const ObjectId extraId = require(study.document.addObject(std::move(*extra)));

    const Document loaded = roundTrip(study.document, temp.path() / "two.bcad");
    const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(loaded);
    REQUIRE(ids.size() == 2U);
    CHECK(ids[0] == study.analysis);
    CHECK(ids[1].value() == extraId.value());
    CHECK(thermal::findThermalAnalysis(loaded, ids[0])->definition() == first);
    CHECK(thermal::findThermalAnalysis(loaded, ids[1])->definition() == second);
    CHECK(thermal::findThermalAnalysis(loaded, ids[1])->name() == "Second");
}

// ---------------------------------------------------------------------------
// GeometryReference
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_GeometryReferencesSurviveExactlyInEveryForm", "[thermal][io][persist][reference]") {
    // Every selector form the data model accepts -- start and end cap, side faces with and
    // without a path, a chamfer, a copied face with its copies IN ORDER, and a face of a feature
    // that does not exist -- on a load AND a condition, compared field for field.
    TempDir temp;
    VizStudy study;
    const std::vector<FaceName> faces = cmdtest::everyFaceForm(study.feature);
    ThermalAnalysisDefinition definition;
    definition.mesh = study.control;
    definition.solver = settings();
    std::uint64_t id = 1;
    for (const FaceName& face : faces) {
        definition.loads.push_back(vztest::surfaceLoad(id, face, 100.0 + static_cast<double>(id)));
        definition.boundaryConditions.push_back(vztest::temperatureBc(id, face, 300.0 + static_cast<double>(id)));
        ++id;
    }
    vztest::define(study, definition);
    const Document loaded = roundTrip(study.document, temp.path() / "faces.bcad");
    const auto& after = onlyAnalysis(loaded).definition();
    REQUIRE(after.loads.size() == faces.size());
    for (std::size_t i = 0; i < faces.size(); ++i) {
        INFO(describe(faces[i]));
        REQUIRE(after.loads[i].surfaceHeatFlux() != nullptr);
        CHECK(after.loads[i].surfaceHeatFlux()->face == faces[i]);
        CHECK(after.boundaryConditions[i].face() == faces[i]);
        CHECK(describe(after.loads[i].surfaceHeatFlux()->face) == describe(faces[i]));
    }
    CHECK(faces.size() == 8U);
}

TEST_CASE("ThermalPersistence_AnUnresolvedReferenceIsPreservedAndNotRebound",
          "[thermal][io][persist][reference]") {
    TempDir temp;
    VizStudy study;
    const FaceName missing{study.feature, FaceSelector{.role = FaceRole::Side,
                                                       .entity = EntityId::fromValue(9991U)}};
    ThermalAnalysisDefinition definition = steadyGolden(study);
    definition.loads.push_back(vztest::surfaceLoad(8, missing, 500.0));
    definition.boundaryConditions.push_back(vztest::temperatureBc(9, missing, 310.0));
    vztest::define(study, definition);

    // IT REALLY DOES NOT RESOLVE BEFORE THE SAVE, or the test would be about nothing.
    const std::unique_ptr<vztest::Derived> before = vztest::derive(study);
    CHECK_FALSE(before->post.has_value());
    CHECK_FALSE(before->stoppedBecause.empty());

    const Document loaded = roundTrip(study.document, temp.path() / "unresolved.bcad");
    const auto& after = onlyAnalysis(loaded).definition();
    REQUIRE(after.loads.size() == 4U);
    REQUIRE(after.loads[3].surfaceHeatFlux() != nullptr);
    CHECK(after.loads[3].surfaceHeatFlux()->face == missing);
    CHECK(after.loads[3].surfaceHeatFlux()->flux.si() == 500.0);
    CHECK(after.boundaryConditions[2].face() == missing);
    // NOT REBOUND: every face the body actually has is a face it could have been rebound TO.
    for (const FaceName& real : {study.startCap(), study.endCap(), study.side(0), study.side(1),
                                 study.side(2), study.side(3)}) {
        INFO(describe(real));
        CHECK(after.loads[3].surfaceHeatFlux()->face != real);
        CHECK(after.boundaryConditions[2].face() != real);
    }
    REQUIRE(after.loads[3].surfaceHeatFlux()->face.face.entity.has_value());
    CHECK(after.loads[3].surfaceHeatFlux()->face.face.entity->value() == 9991U);
}

TEST_CASE("ThermalPersistence_AnUnresolvedReferenceStillRefusesTheSolveAfterLoading",
          "[thermal][io][persist][reference][resolve]") {
    // Persistence has done its job by preserving the intent; refusing is the derived chain's.
    // This is the half that proves the preserved reference is still UNRESOLVED rather than
    // quietly made to work -- and that the validator says so.
    TempDir temp;
    VizStudy study;
    ThermalAnalysisDefinition definition = steadyGolden(study);
    definition.loads.push_back(vztest::surfaceLoad(
        8, FaceName{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}},
        500.0));
    vztest::define(study, definition);

    VizStudy fresh;
    adopt(fresh, roundTrip(study.document, temp.path() / "unresolved-solve.bcad"));
    const std::unique_ptr<vztest::Derived> derived = vztest::derive(fresh);
    CHECK_FALSE(derived->post.has_value());
    CHECK_FALSE(derived->stoppedBecause.empty());
    const thermal::ThermalValidationReport report = vztest::validationOf(fresh, *derived);
    CHECK(report.status() == thermal::ValidationStatus::Fail);
}

TEST_CASE("ThermalPersistence_AMalformedReferenceIsNotTheSameAsAnUnresolvedOne",
          "[thermal][io][persist][malformed][reference]") {
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    REQUIRE(io::documentFromJson(good).has_value());
    const auto condition = [&](std::string_view face) {
        return steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": 300.0}}], {})",
                                            face, kSolver));
    };
    const std::string body = std::format("{}", study.feature.value());

    SECTION("a role this build does not know") {
        const Error error = loadFailure(withAnalysisData(
            good, condition(std::format(R"({{"feature": {}, "face": {{"role": "not_a_role"}}}})", body))));
        CHECK_THAT(error.message, ContainsSubstring("boundary_conditions[0].face"));
    }
    SECTION("a side face naming no entity") {
        const Error error = loadFailure(withAnalysisData(
            good, condition(std::format(R"({{"feature": {}, "face": {{"role": "side"}}}})", body))));
        CHECK_THAT(error.message, ContainsSubstring("boundary_conditions[0].face"));
    }
    SECTION("a reference that is not an object") {
        const Error error = loadFailure(withAnalysisData(good, condition("42")));
        CHECK_THAT(error.message, ContainsSubstring("boundary_conditions[0].face"));
    }
    SECTION("a missing face") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "temperature": 300.0}}], {})", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring(".boundary_conditions[0].face"));
    }
    SECTION("a mesh-local target instead of a reference") {
        // The brief's mesh-local binding attack: a node, an element, a facet or a DOF is not a
        // canonical target. Refused as an unknown key, never read.
        for (const char* key : {"node", "element", "facet", "dof", "node_id", "element_id"}) {
            INFO(key);
            const Error error = loadFailure(withAnalysisData(
                good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": 300.0, "{}": 3}}], {})",
                                                    faceJson(study, "end_cap"), key, kSolver))));
            CHECK_THAT(error.message, ContainsSubstring(key));
        }
    }
    SECTION("a WELL-FORMED reference to a face that does not exist LOADS") {
        const std::string text = withAnalysisData(
            good, condition(std::format(R"({{"feature": {}, "face": {{"role": "side", "entity": 9991}}}})", body)));
        Result<Document> loaded = io::documentFromJson(text);
        INFO((loaded ? std::string{} : loaded.error().message));
        REQUIRE(loaded.has_value());
        const auto& after = onlyAnalysis(*loaded).definition();
        REQUIRE(after.boundaryConditions.size() == 1U);
        CHECK(after.boundaryConditions.front().face().face.entity->value() == 9991U);
    }
}

// ---------------------------------------------------------------------------
// Units and the exclusion of derived state
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_EngineeringValuesAreWrittenInTheirCoreUnits",
          "[thermal][io][persist][units]") {
    // The file holds what the units framework holds: K, W/m^2, W/m^3, W/(m^2 K), s. No Celsius,
    // no kW, no millimetre, no millisecond -- a conversion would change the digits below.
    VizStudy study;
    vztest::define(study, transientGolden(study));
    const std::string text = analysisSectionOf(saveToString(study.document));
    CHECK_THAT(text, ContainsSubstring("\"flux\": -1234.5"));              // W/m^2 (not -1.2345 kW/m^2)
    CHECK_THAT(text, ContainsSubstring("\"generation\": 67890.25"));       // W/m^3
    CHECK_THAT(text, ContainsSubstring("\"temperature\": 345.125"));       // K (not 71.975 degC)
    CHECK_THAT(text, ContainsSubstring("\"coefficient\": 27.75"));         // W/(m^2 K)
    CHECK_THAT(text, ContainsSubstring("\"ambient\": 291.625"));           // K
    CHECK_THAT(text, ContainsSubstring("\"initial_temperature\": 312.875"));   // K
    CHECK_THAT(text, ContainsSubstring("\"time_step\": 0.125"));           // s (not 125 ms)
    CHECK_THAT(text, ContainsSubstring("\"end_time\": 2.375"));            // s
    // And no Celsius value anywhere.
    CHECK(text.find("71.975") == std::string::npos);
    CHECK(text.find("39.725") == std::string::npos);
}

TEST_CASE("ThermalPersistence_NoDerivedStateReachesTheFile", "[thermal][io][persist][authority]") {
    // A STRUCTURAL CHECK OF THE WRITTEN BYTES, not an argument from the code. Both analyses are
    // SOLVED first, so every one of these exists in the process at the moment of the save.
    for (const bool transient : {false, true}) {
        VizStudy study;
        vztest::define(study, transient ? transientGolden(study) : steadyGolden(study));
        const std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
        INFO(derived->stoppedBecause);
        REQUIRE(derived->post.has_value());
        REQUIRE(derived->balance.has_value());
        const thermal::ThermalValidationReport report = vztest::validationOf(study, *derived);
        (void)report;

        const std::string text = analysisSectionOf(saveToString(study.document));
        // MATCHED AS A KEY with its opening quote, not as a bare substring: `relative_residual_tolerance`
        // contains "residual" and `boundary_conditions` contains "condition".
        for (const std::string_view forbidden :
             {"node", "element", "tetra", "dof", "equation", "constraint", "prepared", "conductivity",
              "capacity", "capacitance", "matrix", "triplet", "csr", "stiffness", "temperatures",
              "history", "gradient", "heat_flux", "balance", "reaction", "imbalance", "residual",
              "validation", "findings", "validated", "result", "solution", "current", "stale",
              "render", "contour", "glyph", "classif", "frame", "factoriz", "facet", "density",
              "specific_heat", "conducti", "revision", "timestamp", "duration", "iteration"}) {
            INFO(forbidden);
            CHECK(text.find(std::format("\"{}", forbidden)) == std::string::npos);
        }
        CHECK_THAT(text, ContainsSubstring("\"thermal-analysis\""));
        CHECK_THAT(text, ContainsSubstring("\"flux\""));
        CHECK_THAT(text, ContainsSubstring("\"boundary_conditions\""));
        CHECK_THAT(text, ContainsSubstring("\"relative_residual_tolerance\""));
        CHECK_THAT(text, ContainsSubstring("\"pivot_floor\""));
    }
}

TEST_CASE("ThermalPersistence_TheMaterialStaysP15sAndIsNotCopiedIntoTheAnalysis",
          "[thermal][io][persist][authority]") {
    // P15 is the sole authority for k, rho and cp (P18-MAT-001). The thermal section carries
    // none of them; the document's material object does; and editing the material in a LOADED
    // document changes the re-solved field, which is what authority means.
    TempDir temp;
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string whole = saveToString(study.document);
    const std::string section = analysisSectionOf(whole);
    CHECK(section.find("\"thermal_conductivity") == std::string::npos);
    CHECK(section.find("\"specific_heat") == std::string::npos);
    CHECK(section.find("\"density") == std::string::npos);
    CHECK(section.find("\"material") == std::string::npos);
    CHECK_THAT(whole, ContainsSubstring("thermal_conductivity"));   // the P15 object has them

    VizStudy fresh;
    adopt(fresh, roundTrip(study.document, temp.path() / "material.bcad"));
    const Semantics base = semanticsOf(fresh);
    fresh.editConductivity(450.0);   // ten times
    fresh.remesh();
    const Semantics edited = semanticsOf(fresh);
    REQUIRE(base.temperatures.size() == edited.temperatures.size());
    double difference = 0.0;
    for (std::size_t n = 0; n < base.temperatures[0].size(); ++n) {
        difference = std::max(difference, std::abs(base.temperatures[0][n] - edited.temperatures[0][n]));
    }
    CHECK(difference > 1.0e-3);
}

// ---------------------------------------------------------------------------
// The byte gate
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_TheFileIsIdenticalWhateverASteadySolveHasDone",
          "[thermal][io][persist][authority][bytes]") {
    // FOUR states plus every derived stage. A format that wrote a result only when it was
    // valid would pass a before/after comparison, so the same canonical intent is saved with
    // no result at all, after each derived stage, with a STALE result and after a FAILED solve
    // -- all byte-for-byte identical.
    VizStudy study;
    vztest::multiFree(study);
    const std::string baseline = saveToString(study.document);   // A: no solve, mesh already generated
    std::vector<std::pair<std::string, std::string>> states;

    std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
    INFO(derived->stoppedBecause);
    REQUIRE(derived->steady.has_value());
    states.emplace_back("after assembly + solve", saveToString(study.document));
    REQUIRE(derived->post.has_value());
    states.emplace_back("after POST", saveToString(study.document));
    REQUIRE(derived->balance.has_value());
    states.emplace_back("after BALANCE", saveToString(study.document));
    const thermal::ThermalValidationReport report = vztest::validationOf(study, *derived);
    states.emplace_back("after VALID", saveToString(study.document));
    const renderer::ThermalDisplayState display = vztest::displayStateOf(study, derived.get(), &report);
    (void)display;
    states.emplace_back("after VIZ state", saveToString(study.document));

    // A STALE result: a remesh gives a new MeshStamp and stales everything, changing no
    // document object.
    study.remesh();
    states.emplace_back("with a stale result", saveToString(study.document));
    const renderer::ThermalDisplayState staleDisplay = vztest::displayStateOf(study, derived.get(), &report);
    (void)staleDisplay;
    states.emplace_back("after a stale display", saveToString(study.document));

    // A FAILED solve: a fresh Mesher has generated nothing, so the production path refuses.
    meshing::Mesher empty;
    const Result<thermal::ThermalModel> refused =
        thermal::requireThermalModel(study.document, study.regenerator, empty, study.control);
    CHECK_FALSE(refused.has_value());
    states.emplace_back("after a failed solve attempt", saveToString(study.document));

    for (const auto& [label, bytes] : states) {
        INFO(label);
        CHECK(bytes == baseline);
    }
    // Direct byte comparison with the first differing offset on failure.
    for (const auto& [label, bytes] : states) {
        const auto [a, b] = std::mismatch(baseline.begin(), baseline.end(), bytes.begin(), bytes.end());
        INFO(std::format("{}: first difference at byte {}", label, a - baseline.begin()));
        CHECK((a == baseline.end() && b == bytes.end()));
    }
    WARN(std::format("steady: {} bytes before every stage and after every stage ({} states compared)",
                     baseline.size(), states.size()));
    // NOT VACUOUS: a solve really happened and really produced numbers.
    REQUIRE(derived->post.has_value());
    CHECK(derived->post->nodeCount() > 0U);
    CHECK(derived->post->states().front().temperatures.size() == derived->post->nodeCount());
}

TEST_CASE("ThermalPersistence_TheFileIsIdenticalWhateverALargeTransientSolveHasDone",
          "[thermal][io][persist][authority][bytes]") {
    // The same, for a transient analysis whose result holds two hundred states on a finer mesh.
    // If a single byte of it reached the file, the size would move.
    VizStudy study(std::nullopt, 3_mm);
    vztest::define(study, longTransient(study));
    const std::string baseline = saveToString(study.document);

    std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
    INFO(derived->stoppedBecause);
    REQUIRE(derived->transient.has_value());
    REQUIRE(derived->post.has_value());
    REQUIRE(derived->balance.has_value());
    const std::size_t states = derived->post->stateCount();
    const std::size_t resultValues = states * derived->post->nodeCount();
    REQUIRE(states >= 200U);

    const std::string afterSolve = saveToString(study.document);
    const thermal::ThermalValidationReport report = vztest::validationOf(study, *derived);
    const std::string afterValid = saveToString(study.document);
    const renderer::ThermalDisplayState display = vztest::displayStateOf(study, derived.get(), &report);
    (void)display;
    const std::string afterViz = saveToString(study.document);

    CHECK(afterSolve == baseline);
    CHECK(afterValid == baseline);
    CHECK(afterViz == baseline);
    CHECK(afterViz.size() == baseline.size());
    WARN(std::format("transient: {} bytes before and after a result of {} states x {} nodes = {} temperatures "
                     "({} elements); the file did not grow",
                     baseline.size(), states, derived->post->nodeCount(), resultValues,
                     derived->post->elementCount()));
    // The result is, by a wide margin, bigger than the whole file: it could not have fit.
    // Counted as the post result holds it -- the nodal temperatures and, per element, three
    // gradient and three flux components -- in every stored state.
    const std::size_t resultBytes =
        states * (derived->post->nodeCount() + 6U * derived->post->elementCount()) * sizeof(double);
    WARN(std::format("the result holds {} bytes of numbers; the file is {} bytes", resultBytes, baseline.size()));
    CHECK(resultBytes > baseline.size() * 100U);
}

TEST_CASE("ThermalPersistence_TheFileDoesNotFollowTheMeshOrTheResultSize",
          "[thermal][io][persist][authority][bytes]") {
    // The same canonical thermal intent on meshes of very different size gives the same
    // section. A CYLINDER, because a block cannot demonstrate it: its boundary is planar and
    // exactly representable, so two sizes give one mesh.
    const auto sectionAt = [](std::optional<Length> target) {
        VizStudy study(target, std::nullopt, true);
        ThermalAnalysisDefinition definition = transientGolden(study);
        // the cylinder's caps and wall: side(0) is the curved wall
        definition.loads = {vztest::surfaceLoad(1, study.side(0), kSink), vztest::generationLoad(2, kGeneration)};
        definition.boundaryConditions = {vztest::temperatureBc(1, study.startCap(), kHeld),
                                         vztest::convectionBc(2, study.endCap(), kCoefficient, kAmbientK)};
        vztest::define(study, definition);
        const std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
        INFO(derived->stoppedBecause);
        REQUIRE(derived->post.has_value());
        return std::pair{study.mesh().elementCount(), analysisSectionOf(saveToString(study.document))};
    };
    const auto [coarseElements, coarseText] = sectionAt(Length::fromSi(0.02));
    const auto [fineElements, fineText] = sectionAt(Length::fromSi(0.0015));
    REQUIRE(fineElements > coarseElements);   // the refinement did something
    CHECK(coarseText == fineText);
    WARN(std::format("coarse {} elements | fine {} elements | thermal section {} bytes, equal",
                     coarseElements, fineElements, coarseText.size()));
}

TEST_CASE("ThermalPersistence_SavingChangesNothingAboutTheDocument", "[thermal][io][persist][authority]") {
    TempDir temp;
    VizStudy study;
    vztest::multiFree(study);
    const std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
    REQUIRE(derived->post.has_value());
    const ThermalAnalysis& analysis = onlyAnalysis(study.document);
    const std::uint64_t revisionBefore = analysis.revision();
    const std::uint64_t documentBefore = study.document.revision();
    const std::string intentBefore = fingerprint(analysis);
    const thermal::ThermalValidationReport report = vztest::validationOf(study, *derived);
    const renderer::ThermalDisplayState before = vztest::displayStateOf(study, derived.get(), &report);

    REQUIRE(io::saveDocument(study.document, temp.path() / "unchanged.bcad").has_value());

    CHECK(onlyAnalysis(study.document).revision() == revisionBefore);
    CHECK(study.document.revision() == documentBefore);
    CHECK(fingerprint(onlyAnalysis(study.document)) == intentBefore);
    // The result was current before the save and is still current after it.
    const renderer::ThermalDisplayState after = vztest::displayStateOf(study, derived.get(), &report);
    CHECK(after.result == before.result);
    CHECK(after.result == renderer::ThermalResultState::Current);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_SerializationIsByteIdenticalRepeatedlyAndIndependentOfHowItWasBuilt",
          "[thermal][io][persist][determinism]") {
    VizStudy study;
    vztest::define(study, transientGolden(study));

    SECTION("ten saves of one document are identical") {
        const std::string first = saveToString(study.document);
        for (int i = 0; i < 10; ++i) {
            INFO(i);
            CHECK(saveToString(study.document) == first);
        }
        WARN(std::format("whole file {} bytes, thermal section {} bytes", first.size(),
                         analysisSectionOf(first).size()));
    }

    SECTION("the same state reached through the command history is identical") {
        // The brief's history argument, answered for P18-CMD: the same final canonical state is
        // built up through commands with an undo and a redo in the middle, and the section must be
        // identical -- a format that stored a revision, an undo depth or an edit count would differ.
        VizStudy other;
        ThermalAnalysisDefinition start;
        start.mesh = other.control;
        start.mode = ThermalAnalysisMode::Transient;
        start.solver = settings();
        start.initialCondition = UniformInitialTemperature{Temperature::fromSi(300.0)};
        start.transient = TransientSettings{.timeStep = Time::fromSi(1.0), .endTime = Time::fromSi(2.0)};
        vztest::define(other, start);
        const ThermalAnalysisDefinition target = transientGolden(other);

        CommandHistory history;
        const ThermalAnalysisId id = other.analysis;
        const auto run = [&](std::unique_ptr<Command> command) {
            INFO(command->description());
            REQUIRE(history.execute(other.document, std::move(command)).has_value());
        };
        run(std::make_unique<thermal::AddThermalLoadCommand>(id, target.loads[0]));
        run(std::make_unique<thermal::AddThermalLoadCommand>(id, vztest::generationLoad(2, 1.0)));   // later edited
        run(std::make_unique<thermal::EditThermalLoadCommand>(id, target.loads[1]));
        run(std::make_unique<thermal::AddThermalLoadCommand>(id, target.loads[2]));
        run(std::make_unique<thermal::AddThermalBoundaryConditionCommand>(id, target.boundaryConditions[0]));
        run(std::make_unique<thermal::AddThermalBoundaryConditionCommand>(id, target.boundaryConditions[1]));
        run(std::make_unique<thermal::SetInitialTemperatureCommand>(id, *target.initialCondition));
        run(std::make_unique<thermal::SetTransientSettingsCommand>(id, *target.transient));
        run(std::make_unique<thermal::SetThermalSolverSettingsCommand>(id, target.solver));
        // an undo and a redo in the middle changes the revisions and nothing canonical
        REQUIRE(history.undo(other.document).has_value());
        REQUIRE(history.redo(other.document).has_value());
        REQUIRE(history.undoCount() == 9U);

        CHECK(onlyAnalysis(other.document).definition() == onlyAnalysis(study.document).definition());
        CHECK(analysisSectionOf(saveToString(other.document)) == analysisSectionOf(saveToString(study.document)));
    }
}

TEST_CASE("ThermalPersistence_SaveLoadSaveIsByteIdenticalAndAFixedPoint", "[thermal][io][persist][determinism]") {
    TempDir temp;
    VizStudy study;
    vztest::define(study, transientGolden(study));

    const std::filesystem::path first = temp.path() / "first.bcad";
    REQUIRE(io::saveDocument(study.document, first).has_value());
    const std::string save1 = readFile(first);
    Result<Document> loaded = io::loadDocument(first);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    const std::filesystem::path second = temp.path() / "second.bcad";
    REQUIRE(io::saveDocument(*loaded, second).has_value());
    const std::string save2 = readFile(second);
    CHECK(save1 == save2);
    // A FIXED POINT, not merely a pair: a third pass must agree too.
    Result<Document> again = io::documentFromJson(save2);
    REQUIRE(again.has_value());
    CHECK(saveToString(*again) == save2);
}

TEST_CASE("ThermalPersistence_AnExplicitEmptyCollectionNormalisesThenIsByteStable",
          "[thermal][io][persist][determinism]") {
    // BetterCAD omits an empty collection, so a hand-written file carrying `"loads": []` is
    // well-formed but is not what this build writes: the first save normalises it away and
    // every save after that is identical.
    VizStudy study;
    ThermalAnalysisDefinition definition;
    definition.mesh = study.control;
    definition.solver = settings();
    vztest::define(study, definition);
    const std::string canonical = saveToString(study.document);

    const std::string handWritten = withAnalysisData(
        canonical,
        std::format(R"({{"mesh": {}, "mode": "steady", "loads": [], "boundary_conditions": [],
            "solver": {{"relative_residual_tolerance": {:.17g}, "pivot_floor": {:.17g}}}}})",
                    study.control.value(), kTolerance, kPivotFloor));
    Result<Document> loaded = io::documentFromJson(handWritten);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    const std::string first = saveToString(*loaded);
    CHECK(analysisSectionOf(first) == analysisSectionOf(canonical));
    Result<Document> again = io::documentFromJson(first);
    REQUIRE(again.has_value());
    CHECK(saveToString(*again) == first);
}

TEST_CASE("ThermalPersistence_TheDecimalPointDoesNotDependOnTheLocale", "[thermal][io][persist][determinism]") {
    // A German-style locale writes 0,3. The file must not follow it. The locale may not be
    // installed on this machine, in which case the test says so and does not pretend.
    VizStudy study;
    vztest::define(study, transientGolden(study));
    const std::string reference = saveToString(study.document);
    const char* previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string restore = previous != nullptr ? previous : "C";
    bool switched = false;
    for (const char* name : {"de_DE.UTF-8", "German_Germany.1252", "German", "de-DE", "fr-FR", "fr_FR.UTF-8", "French_France.1252", "French"}) {
        if (std::setlocale(LC_NUMERIC, name) != nullptr) {
            const std::lconv* conv = std::localeconv();
            if (conv != nullptr && std::string_view{conv->decimal_point} == ",") {
                switched = true;
                break;
            }
        }
    }
    const std::string underLocale = saveToString(study.document);
    std::setlocale(LC_NUMERIC, restore.c_str());
    CHECK(underLocale == reference);
    CHECK(underLocale.find("0,125") == std::string::npos);
    CHECK_THAT(underLocale, ContainsSubstring("\"time_step\": 0.125"));
    if (!switched) {
        WARN("no comma-decimal locale is installed here; the file was written under the C locale only");
    }
}

// ---------------------------------------------------------------------------
// Malformed, invalid and duplicate
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_RejectsMalformedThermalIntent", "[thermal][io][persist][malformed]") {
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    REQUIRE(io::documentFromJson(good).has_value());
    const auto mesh = study.control.value();
    const std::string end = faceJson(study, "end_cap");

    SECTION("data that is not an object") {
        // `withAnalysisData` can only substitute a {...} block, so the whole `data` value is swapped here.
        for (const char* nonObject : {"[]", "3", "null", "\"x\"", "true"}) {
            INFO(nonObject);
            const std::size_t typeAt = good.find(kMarker);
            const std::size_t dataAt = good.find("\"data\":", typeAt);
            const std::size_t open = good.find('{', dataAt);
            std::size_t depth = 0;
            std::size_t close = open;
            for (; close < good.size(); ++close) {
                depth += good[close] == '{' ? 1U : 0U;
                if (good[close] == '}' && --depth == 0U) {
                    break;
                }
            }
            const Error error = loadFailure(good.substr(0, open) + nonObject + good.substr(close + 1));
            CHECK_THAT(error.message, ContainsSubstring(".data: expected an object"));
        }
        // and an object with nothing in it is refused for the first thing it lacks
        CHECK_THAT(loadFailure(withAnalysisData(good, "{}")).message, ContainsSubstring(".data.mesh"));
    }
    SECTION("an unknown top-level field") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"({}, "ordering": "amd")", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("ordering"));
    }
    SECTION("a missing mesh control") {
        const Error error = loadFailure(withAnalysisData(good, std::format(R"({{"mode": "steady", {}}})", kSolver)));
        CHECK_THAT(error.message, ContainsSubstring(".mesh"));
    }
    SECTION("an invalid mesh handle") {
        const Error error = loadFailure(
            withAnalysisData(good, std::format(R"({{"mesh": 0, "mode": "steady", {}}})", kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("invalid handle cannot name the mesh"));
    }
    SECTION("a missing mode") {
        const Error error = loadFailure(withAnalysisData(good, std::format(R"({{"mesh": {}, {}}})", mesh, kSolver)));
        CHECK_THAT(error.message, ContainsSubstring(".mode"));
    }
    SECTION("an unknown mode, never mapped to an existing one") {
        const Error error = loadFailure(withAnalysisData(
            good, std::format(R"({{"mesh": {}, "mode": "nonlinear", {}}})", mesh, kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("unknown analysis mode 'nonlinear'"));
    }
    SECTION("a mode of the wrong type") {
        const Error error = loadFailure(withAnalysisData(
            good, std::format(R"({{"mesh": {}, "mode": 1, {}}})", mesh, kSolver)));
        CHECK_THAT(error.message, ContainsSubstring(".mode"));
    }
    SECTION("a missing solver section") {
        const Error error = loadFailure(withAnalysisData(good, std::format(R"({{"mesh": {}, "mode": "steady"}})", mesh)));
        CHECK_THAT(error.message, ContainsSubstring(".solver"));
    }
    SECTION("loads that are not an array") {
        const Error error = loadFailure(withAnalysisData(good, steadyData(study, std::format(R"("loads": 3, {})", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring(".loads"));
    }
    SECTION("a load that is not an object") {
        const Error error = loadFailure(withAnalysisData(good, steadyData(study, std::format(R"("loads": [7], {})", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("loads[0]"));
    }
    SECTION("an unknown load type, never mapped to a default") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "radiation"}}], {})", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("unknown load type 'radiation'"));
    }
    SECTION("an unknown condition type, never mapped to a default") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "radiation", "face": {}}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("unknown boundary condition type 'radiation'"));
    }
    SECTION("a surface flux with no value") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "surface_heat_flux", "face": {}}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring(".loads[0].flux"));
    }
    SECTION("a surface flux with no face") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "surface_heat_flux", "flux": 5.0}}], {})", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring(".loads[0].face"));
    }
    SECTION("a volumetric source carrying a face") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "volumetric_heat_source", "generation": 5.0, "face": {}}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("volumetric heat source load does not carry this"));
    }
    SECTION("a surface flux carrying a generation") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "surface_heat_flux", "face": {}, "flux": 5.0, "generation": 5.0}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("surface heat flux load does not carry this"));
    }
    SECTION("a prescribed temperature carrying a coefficient") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": 300.0, "coefficient": 5.0}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("prescribed temperature condition does not carry this"));
    }
    SECTION("a convection missing its ambient") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "convection", "face": {}, "coefficient": 5.0}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring(".boundary_conditions[0].ambient"));
    }
    SECTION("a load with no id") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"type": "volumetric_heat_source", "generation": 5.0}}], {})", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring(".loads[0].id"));
    }
    SECTION("keys a kind must not carry, in every direction") {
        const Error flux = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "volumetric_heat_source", "generation": 5.0, "flux": 5.0}}], {})", kSolver))));
        CHECK_THAT(flux.message, ContainsSubstring("volumetric heat source load does not carry this"));
        const Error temperature = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "convection", "face": {}, "coefficient": 5.0, "ambient": 293.15, "temperature": 300.0}}], {})", end, kSolver))));
        CHECK_THAT(temperature.message, ContainsSubstring("convection condition does not carry this"));
        const Error ambient = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": 300.0, "ambient": 293.15}}], {})", end, kSolver))));
        CHECK_THAT(ambient.message, ContainsSubstring("prescribed temperature condition does not carry this"));
    }
    SECTION("a prescribed temperature with no temperature") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {}}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring(".boundary_conditions[0].temperature"));
    }
    SECTION("a mesh handle that is a string or a float") {
        for (const char* handle : {"\"7\"", "7.5", "-1", "null"}) {
            INFO(handle);
            const Error error = loadFailure(withAnalysisData(
                good, std::format(R"({{"mesh": {}, "mode": "steady", {}}})", handle, kSolver)));
            CHECK_THAT(error.message, ContainsSubstring(".data.mesh"));
        }
    }
    SECTION("settings and sections of the wrong type") {
        const Error solverString = loadFailure(withAnalysisData(good, steadyData(study, R"("solver": "default")")));
        CHECK_THAT(solverString.message, ContainsSubstring(".solver"));
        const Error solverNull = loadFailure(withAnalysisData(good, steadyData(study, R"("solver": null)")));
        CHECK_THAT(solverNull.message, ContainsSubstring(".solver"));
        const Error transientString = loadFailure(withAnalysisData(
            good, transientData(study, std::format(R"("initial_temperature": 300.0, {}, "transient": "fast")", kSolver))));
        CHECK_THAT(transientString.message, ContainsSubstring(".transient"));
        const Error initialString = loadFailure(withAnalysisData(
            good, transientData(study, std::format(R"("initial_temperature": "warm", {}, "transient": {{"time_step": 0.5, "end_time": 2.0, "output": "final_only"}})", kSolver))));
        CHECK_THAT(initialString.message, ContainsSubstring(".initial_temperature"));
        const Error stepString = loadFailure(withAnalysisData(
            good, transientData(study, std::format(R"("initial_temperature": 300.0, {}, "transient": {{"time_step": "0.5", "end_time": 2.0, "output": "final_only"}})", kSolver))));
        CHECK_THAT(stepString.message, ContainsSubstring(".transient.time_step"));
        const Error endString = loadFailure(withAnalysisData(
            good, transientData(study, std::format(R"("initial_temperature": 300.0, {}, "transient": {{"time_step": 0.5, "end_time": "2.0", "output": "final_only"}})", kSolver))));
        CHECK_THAT(endString.message, ContainsSubstring(".transient.end_time"));
    }
    SECTION("a solver section missing a field is not defaulted") {
        const Error missingPivot = loadFailure(withAnalysisData(
            good, steadyData(study, R"("solver": {"relative_residual_tolerance": 1e-9})")));
        CHECK_THAT(missingPivot.message, ContainsSubstring(".solver.pivot_floor"));
        const Error missingTolerance =
            loadFailure(withAnalysisData(good, steadyData(study, R"("solver": {"pivot_floor": 1e-12})")));
        CHECK_THAT(missingTolerance.message, ContainsSubstring(".solver.relative_residual_tolerance"));
    }
    SECTION("an unknown solver field") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, R"("solver": {"relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12, "algorithm": "pardiso"})")));
        CHECK_THAT(error.message, ContainsSubstring("algorithm"));
    }
}

TEST_CASE("ThermalPersistence_RejectsWrongFieldTypes", "[thermal][io][persist][malformed]") {
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    const std::string end = faceJson(study, "end_cap");
    const auto convection = [&](std::string_view fields) {
        return withAnalysisData(good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "convection", "face": {}, {}}}], {})", end, fields, kSolver)));
    };
    SECTION("a coefficient given as a string, never parsed") {
        const Error error = loadFailure(convection(R"("coefficient": "twenty-five", "ambient": 293.15)"));
        CHECK_THAT(error.message, ContainsSubstring(".boundary_conditions[0].coefficient"));
    }
    SECTION("an ambient given as a bool") {
        const Error error = loadFailure(convection(R"("coefficient": 25.0, "ambient": true)"));
        CHECK_THAT(error.message, ContainsSubstring(".boundary_conditions[0].ambient"));
    }
    SECTION("a value given as null") {
        const Error error = loadFailure(convection(R"("coefficient": null, "ambient": 293.15)"));
        CHECK_THAT(error.message, ContainsSubstring(".boundary_conditions[0].coefficient"));
    }
    SECTION("a number given as an array") {
        const Error error = loadFailure(convection(R"("coefficient": [25.0], "ambient": 293.15)"));
        CHECK_THAT(error.message, ContainsSubstring(".boundary_conditions[0].coefficient"));
    }
    SECTION("an id given as a string, a float, a negative or an overflow") {
        for (const char* id : {"\"7\"", "7.5", "-1", "9007199254740993", "18446744073709551616", "1e30"}) {
            INFO(id);
            const Error error = loadFailure(withAnalysisData(
                good, steadyData(study, std::format(R"("loads": [{{"id": {}, "type": "volumetric_heat_source", "generation": 5.0}}], {})", id, kSolver))));
            CHECK_THAT(error.message, ContainsSubstring(".loads[0].id"));
        }
    }
    SECTION("the largest permitted id is accepted and the invalid id is not") {
        CHECK(io::documentFromJson(withAnalysisData(
                  good, steadyData(study, std::format(R"("loads": [{{"id": 9007199254740991, "type": "volumetric_heat_source", "generation": 5.0}}], {})", kSolver))))
                  .has_value());
        const Error zero = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 0, "type": "volumetric_heat_source", "generation": 5.0}}], {})", kSolver))));
        CHECK_THAT(zero.message, ContainsSubstring("ThermalLoadId"));
        // at the RECORD's path: it is the reader's per-record call of the core validator that names it
        // (the definition-level validation that follows would name only the analysis)
        CHECK_THAT(zero.message, ContainsSubstring(".loads[0]"));
    }
}

TEST_CASE("ThermalPersistence_RejectsInvalidValuesRatherThanCorrectingThem", "[thermal][io][persist][malformed]") {
    VizStudy study;
    vztest::define(study, transientGolden(study));
    const std::string good = saveToString(study.document);
    const std::string end = faceJson(study, "end_cap");
    const auto steadyWith = [&](std::string_view body) {
        return withAnalysisData(good, steadyData(study, std::format("{}, {}", body, kSolver)));
    };
    const auto transientWith = [&](std::string_view initial, std::string_view transient, std::string_view solver = kSolver) {
        return withAnalysisData(good, transientData(study, std::format(R"({}, {}, "transient": {})", initial, solver, transient)));
    };
    constexpr std::string_view kInitial = R"("initial_temperature": 300.0)";
    constexpr std::string_view kGoodTransient = R"({"time_step": 0.5, "end_time": 2.0, "output": "every_step"})";
    REQUIRE(io::documentFromJson(transientWith(kInitial, kGoodTransient)).has_value());

    SECTION("a convection coefficient of zero, negative or absent: never abs()'d") {
        for (const char* h : {"0.0", "-25.0", "-1e-300"}) {
            INFO(h);
            const Error error = loadFailure(steadyWith(std::format(R"("boundary_conditions": [{{"id": 1, "type": "convection", "face": {}, "coefficient": {}, "ambient": 293.15}}])", end, h)));
            CHECK(error.code == ErrorCode::InvalidArgument);
            CHECK_THAT(error.message, ContainsSubstring("boundary_conditions[0]"));
        }
    }
    SECTION("a temperature below absolute zero, in any role") {
        const Error held = loadFailure(steadyWith(std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": -273.15}}])", end)));
        CHECK(held.code == ErrorCode::InvalidArgument);
        const Error ambient = loadFailure(steadyWith(std::format(R"("boundary_conditions": [{{"id": 1, "type": "convection", "face": {}, "coefficient": 5.0, "ambient": -1.0}}])", end)));
        CHECK(ambient.code == ErrorCode::InvalidArgument);
        const Error initial = loadFailure(transientWith(R"("initial_temperature": -0.5)", kGoodTransient));
        CHECK(initial.code == ErrorCode::InvalidArgument);
        CHECK_THAT(initial.message, ContainsSubstring("initial_temperature"));
    }
    SECTION("zero kelvin is the limit of the scale and is accepted") {
        CHECK(io::documentFromJson(transientWith(R"("initial_temperature": 0.0)", kGoodTransient)).has_value());
    }
    SECTION("a non-finite value cannot even be expressed") {
        // MEASURED: the parser refuses an out-of-range literal outright, so an infinity never
        // becomes a number for the reader or for the core's validator.
        const Error error = loadFailure(steadyWith(R"("loads": [{"id": 1, "type": "volumetric_heat_source", "generation": 1e999}])"));
        CHECK(error.code == ErrorCode::ParseError);
        CHECK_THAT(error.message, ContainsSubstring("number overflow"));
        const Error pivot = loadFailure(withAnalysisData(good, transientData(study, std::format(R"({}, "solver": {{"relative_residual_tolerance": 1e-9, "pivot_floor": 1e999}}, "transient": {})", kInitial, kGoodTransient))));
        CHECK(pivot.code == ErrorCode::ParseError);
        // NaN and Infinity are not JSON at all
        for (const char* token : {"NaN", "Infinity", "-Infinity", "nan"}) {
            INFO(token);
            CHECK_FALSE(io::documentFromJson(steadyWith(std::format(R"("loads": [{{"id": 1, "type": "volumetric_heat_source", "generation": {}}}])", token))).has_value());
        }
    }
    SECTION("solver tolerances that are zero, negative or missing are never defaulted") {
        for (const char* solver : {
                 R"("solver": {"relative_residual_tolerance": 0.0, "pivot_floor": 1e-12})",
                 R"("solver": {"relative_residual_tolerance": -1e-9, "pivot_floor": 1e-12})",
                 R"("solver": {"relative_residual_tolerance": 1e-9, "pivot_floor": 0.0})",
                 R"("solver": {"relative_residual_tolerance": 1e-9, "pivot_floor": -1e-12})"}) {
            INFO(solver);
            const Error error = loadFailure(transientWith(kInitial, kGoodTransient, solver));
            CHECK(error.code == ErrorCode::InvalidArgument);
            CHECK_THAT(error.message, ContainsSubstring(".solver"));
        }
    }
    SECTION("transient settings that are zero, negative, longer than the span or unknown are never replaced by a default") {
        for (const char* transient : {
                 R"({"time_step": 0.0, "end_time": 2.0, "output": "every_step"})",
                 R"({"time_step": -0.5, "end_time": 2.0, "output": "every_step"})",
                 R"({"time_step": 0.5, "end_time": 0.0, "output": "every_step"})",
                 R"({"time_step": 0.5, "end_time": -2.0, "output": "every_step"})",
                 R"({"time_step": 3.0, "end_time": 2.0, "output": "every_step"})"}) {
            INFO(transient);
            const Error error = loadFailure(transientWith(kInitial, transient));
            CHECK(error.code == ErrorCode::InvalidArgument);
            CHECK_THAT(error.message, ContainsSubstring(".transient"));
        }
        const Error unknown = loadFailure(transientWith(kInitial, R"({"time_step": 0.5, "end_time": 2.0, "output": "sometimes"})"));
        CHECK_THAT(unknown.message, ContainsSubstring("unknown output policy 'sometimes'"));
        const Error missing = loadFailure(transientWith(kInitial, R"({"time_step": 0.5, "end_time": 2.0})"));
        CHECK_THAT(missing.message, ContainsSubstring(".transient.output"));
        const Error extra = loadFailure(transientWith(kInitial, R"({"time_step": 0.5, "end_time": 2.0, "output": "every_step", "times": [0.0, 1.0]})"));
        CHECK_THAT(extra.message, ContainsSubstring("times"));
    }
    SECTION("the mode-complete rule: a transient analysis needs both, a steady one carries neither") {
        const Error noInitial = loadFailure(withAnalysisData(good, transientData(study, std::format(R"({}, "transient": {})", kSolver, kGoodTransient))));
        CHECK_THAT(noInitial.message, ContainsSubstring("initial temperature"));
        const Error noTransient = loadFailure(withAnalysisData(good, transientData(study, std::format(R"({}, {})", kInitial, kSolver))));
        CHECK_THAT(noTransient.message, ContainsSubstring("time step"));
        const Error steadyWithInitial = loadFailure(steadyWith(kInitial));
        CHECK_THAT(steadyWithInitial.message, ContainsSubstring("steady analysis has no initial condition"));
        const Error steadyWithTransient = loadFailure(steadyWith(std::format(R"("transient": {})", kGoodTransient)));
        CHECK_THAT(steadyWithTransient.message, ContainsSubstring("steady analysis has no time step"));
    }
    SECTION("negative and zero loads are ordinary") {
        CHECK(io::documentFromJson(steadyWith(R"("loads": [{"id": 1, "type": "volumetric_heat_source", "generation": -5.0}, {"id": 2, "type": "volumetric_heat_source", "generation": 0.0}])")).has_value());
    }
}

TEST_CASE("ThermalPersistence_RejectsDuplicateIdentitiesRatherThanMerging", "[thermal][io][persist][malformed]") {
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    const std::string end = faceJson(study, "end_cap");
    const std::string start = faceJson(study, "start_cap");

    SECTION("two loads sharing a ThermalLoadId") {
        // NOT first-wins, NOT last-wins, NOT merged and NOT regenerated.
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [
                {{"id": 5, "type": "volumetric_heat_source", "generation": 1.0}},
                {{"id": 5, "type": "surface_heat_flux", "face": {}, "flux": 2.0}}], {})", end, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("the load id 5 is used twice"));
        CHECK_THAT(error.message, ContainsSubstring("loads[1].id"));
    }
    SECTION("two conditions sharing a ThermalBoundaryConditionId") {
        const Error error = loadFailure(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [
                {{"id": 2, "type": "prescribed_temperature", "face": {}, "temperature": 300.0}},
                {{"id": 2, "type": "convection", "face": {}, "coefficient": 5.0, "ambient": 293.15}}], {})", end, start, kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("the boundary condition id 2 is used twice"));
        CHECK_THAT(error.message, ContainsSubstring("boundary_conditions[1].id"));
    }
    SECTION("a load id and a condition id may share a numeric value: they are different strong types") {
        Result<Document> loaded = io::documentFromJson(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "volumetric_heat_source", "generation": 1.0}}],
                "boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": 300.0}}], {})", end, kSolver))));
        INFO((loaded ? std::string{} : loaded.error().message));
        REQUIRE(loaded.has_value());
    }
    SECTION("two loads with the same target and value but different ids are NOT duplicates") {
        // Two physical loads on one face are additive; whether that is sensible is the validator's
        // question, not the file's. Persistence preserves both and merges neither.
        Result<Document> loaded = io::documentFromJson(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [
                {{"id": 1, "type": "surface_heat_flux", "face": {}, "flux": 100.0}},
                {{"id": 2, "type": "surface_heat_flux", "face": {}, "flux": 100.0}}], {})", end, end, kSolver))));
        REQUIRE(loaded.has_value());
        CHECK(onlyAnalysis(*loaded).definition().loads.size() == 2U);
    }
    SECTION("two conflicting conditions on one face are preserved too") {
        Result<Document> loaded = io::documentFromJson(withAnalysisData(
            good, steadyData(study, std::format(R"("boundary_conditions": [
                {{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": 300.0}},
                {{"id": 2, "type": "prescribed_temperature", "face": {}, "temperature": 400.0}}], {})", end, end, kSolver))));
        REQUIRE(loaded.has_value());
        CHECK(onlyAnalysis(*loaded).definition().boundaryConditions.size() == 2U);
    }
    SECTION("two analyses sharing an object id") {
        // The analysis id is the document's object id, so this is the document's own rule.
        auto extra = ThermalAnalysis::create("Second", steadyGolden(study));
        REQUIRE(extra.has_value());
        REQUIRE(study.document.addObject(std::move(*extra)).has_value());
        const std::string two = saveToString(study.document);
        REQUIRE(io::documentFromJson(two).has_value());
        const std::size_t second = two.rfind("{\n      \"id\":");
        REQUIRE(second != std::string::npos);
        const std::size_t idAt = two.find("\"id\":", second);
        const std::size_t idEnd = two.find(',', idAt);
        const std::string firstId = [&] {
            const std::size_t first = two.find(kMarker);
            const std::size_t back = two.rfind("\"id\":", first);
            return two.substr(back, two.find(',', back) - back);
        }();
        std::string duplicated = two;
        duplicated.replace(idAt, idEnd - idAt, firstId);
        const Error error = loadFailure(duplicated);
        INFO(error.message);
        CHECK_THAT(error.message, ContainsSubstring("is already in use"));
    }
}

TEST_CASE("ThermalPersistence_FakeResultsAndFakeStatusCannotBecomeAuthority", "[thermal][io][persist][authority][attack]") {
    // A malicious or confused file carrying generated state. The policy is STRICT REJECTION of
    // any unknown key (the whole format's), which is the stronger of the two acceptable outcomes:
    // the fake never becomes anything, because the file is refused by name.
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    const std::string base = std::format(R"("mesh": {}, "mode": "steady", {})", study.control.value(), kSolver);

    for (const char* injected :
         {R"("temperature": [999999.0, 999999.0])", R"("temperatures": [[999999.0]])", R"("heat_flux": [1.0, 2.0, 3.0])",
          R"("gradient": [1.0])", R"("balance": 0.0)", R"("reaction_heat": [0.0])", R"("K": [[1.0]])", R"("C": [[1.0]])",
          R"("Q": [1.0])", R"("validated": true)", R"("current": true)", R"("stale": false)", R"("result": {"temperature": [1.0]})",
          R"("validation": "pass")", R"("dof_map": [1, 2, 3])", R"("prepared_loads": [])", R"("solution": [])"}) {
        INFO(injected);
        const Error error = loadFailure(withAnalysisData(good, std::format("{{{}, {}}}", base, injected)));
        // refused, NAMING THE KEY
        CHECK(error.code == ErrorCode::ParseError);
        const std::string key = std::string{injected}.substr(1, std::string_view{injected}.find('"', 1) - 1);
        CHECK_THAT(error.message, ContainsSubstring(key));
    }
    // The same keys inside a record are refused too.
    for (const char* injected : {R"(, "temperature": 999999.0)", R"(, "validated": true)", R"(, "node": 4)"}) {
        INFO(injected);
        CHECK_FALSE(io::documentFromJson(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "volumetric_heat_source", "generation": 5.0{}}}], {})", injected, kSolver)))).has_value());
    }
}

TEST_CASE("ThermalPersistence_ACorruptFileLeavesTheOpenDocumentAlone", "[thermal][io][persist][malformed][atomic]") {
    // Load atomicity. A failed load publishes nothing. The property is true BY CONSTRUCTION --
    // the loaders build and return a NEW `Document` and there is no entry point that fills an
    // existing one -- and the two static_asserts below pin that shape, so adding an in-place
    // loader would have to change this test. The runtime half then corrupts a file in the
    // places a half-publishing reader would leave something behind (late in the thermal section,
    // after a good analysis, before a good analysis) and requires the open document to be
    // byte-for-byte what it was, with the same diagnostic every time.
    static_assert(std::is_same_v<decltype(io::documentFromJson(std::declval<std::string_view>())), Result<Document>>);
    static_assert(std::is_same_v<decltype(io::loadDocument(std::declval<const std::filesystem::path&>())), Result<Document>>);
    TempDir temp;
    VizStudy study;
    vztest::define(study, transientGolden(study));
    const std::string intentBefore = fingerprint(onlyAnalysis(study.document));
    const std::string good = saveToString(study.document);
    const std::uint64_t revision = onlyAnalysis(study.document).revision();
    const std::string end = faceJson(study, "end_cap");

    const std::vector<std::pair<const char*, std::string>> broken = {
        {"a late wrong-typed field",
         withAnalysisData(good, steadyData(study, std::format(R"("loads": [
            {{"id": 1, "type": "volumetric_heat_source", "generation": 5.0}},
            {{"id": 2, "type": "surface_heat_flux", "face": {}, "flux": 5.0}},
            {{"id": 3, "type": "volumetric_heat_source", "generation": "hot"}}], {})", end, kSolver)))},
        {"a late duplicate id",
         withAnalysisData(good, steadyData(study, std::format(R"("loads": [
            {{"id": 1, "type": "volumetric_heat_source", "generation": 5.0}},
            {{"id": 2, "type": "volumetric_heat_source", "generation": 5.0}},
            {{"id": 1, "type": "volumetric_heat_source", "generation": 5.0}}], {})", kSolver)))},
        {"a late malformed reference",
         withAnalysisData(good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "volumetric_heat_source", "generation": 5.0}}],
            "boundary_conditions": [
            {{"id": 1, "type": "prescribed_temperature", "face": {}, "temperature": 300.0}},
            {{"id": 2, "type": "prescribed_temperature", "face": {{"feature": {}, "face": {{"role": "side"}}}}, "temperature": 300.0}}], {})", end, study.feature.value(), kSolver)))},
        {"late invalid transient settings",
         withAnalysisData(good, transientData(study, std::format(R"("loads": [{{"id": 1, "type": "volumetric_heat_source", "generation": 5.0}}],
            "initial_temperature": 300.0, {}, "transient": {{"time_step": -1.0, "end_time": 2.0, "output": "final_only"}})", kSolver)))},
        {"a truncated file", good.substr(0, good.size() / 2)},
        {"a good analysis followed by a broken object", [&] {
             std::string text = good;
             const std::size_t close = text.rfind("\n  ]");
             text.insert(close, ",\n    {\"id\": 99, \"type\": \"sketch\", \"name\": \"Bad\", \"data\": {}}");
             return text;
         }()},
        {"a broken object followed by a good analysis", [&] {
             std::string text = good;
             const std::size_t open = text.find("\"objects\": [");
             text.insert(open + std::string_view{"\"objects\": ["}.size(),
                         "\n    {\"id\": 98, \"type\": \"sketch\", \"name\": \"Bad\", \"data\": {}},");
             return text;
         }()},
    };
    std::vector<std::string> messages;
    for (const auto& [label, text] : broken) {
        INFO(label);
        const std::filesystem::path file = temp.path() / "corrupt.bcad";
        writeFile(file, text);
        Result<Document> loaded = io::loadDocument(file);
        REQUIRE_FALSE(loaded.has_value());
        // The open document is exactly what it was.
        CHECK(fingerprint(onlyAnalysis(study.document)) == intentBefore);
        CHECK(onlyAnalysis(study.document).revision() == revision);
        CHECK(saveToString(study.document) == good);
        // And the same malformed file gives the same diagnostic again (deterministic).
        Result<Document> again = io::loadDocument(file);
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().message == loaded.error().message);
        CHECK(again.error().code == loaded.error().code);
    }
}

// ---------------------------------------------------------------------------
// Backward compatibility
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_APreP18DocumentLoadsAndSimplyHasNoThermalAnalyses",
          "[thermal][io][persist][compat]") {
    // A document written before P18 existed cannot be refused for lacking something it never had.
    // Both a meshed plate (P16) and an analysed plate (P17) predate P18; neither carries a thermal
    // section; both load, report no thermal analysis, and re-saving invents none. The version is
    // NOT bumped: adding an object type is additive (DocumentFile.hpp's rule).
    for (const char* name : {"meshed_plate.bcad", "analysed_plate.bcad", "plate.bcad"}) {
        INFO(name);
        const std::filesystem::path file = std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / name;
        REQUIRE(std::filesystem::exists(file));
        const std::string text = readFile(file);
        REQUIRE(text.find("thermal-analysis") == std::string::npos);
        Result<Document> loaded = io::loadDocument(file);
        INFO((loaded ? std::string{} : loaded.error().message));
        REQUIRE(loaded.has_value());
        CHECK(thermal::thermalAnalysisIds(*loaded).empty());
        const std::string resaved = saveToString(*loaded);
        CHECK(resaved.find("thermal-analysis") == std::string::npos);
        CHECK(resaved.find("\"version\": 2") != std::string::npos);
    }
}

TEST_CASE("ThermalPersistence_AFutureSchemaVersionIsRefusedNotGuessed", "[thermal][io][persist][compat]") {
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    const std::size_t at = good.find("\"version\": 2");
    REQUIRE(at != std::string::npos);
    std::string future = good;
    future.replace(at, std::string_view{"\"version\": 2"}.size(), "\"version\": 3");
    const Error error = loadFailure(future);
    CHECK_THAT(error.message, ContainsSubstring("version"));
    // And the first P18 file is version 2 -- the number that has never moved.
    CHECK_THAT(good, ContainsSubstring("\"version\": 2"));
}

TEST_CASE("ThermalPersistence_TheFileNeedsNoSolverOrMesherToBeInterpreted", "[thermal][io][persist][compat]") {
    TempDir temp;
    VizStudy study;
    vztest::define(study, transientGolden(study));
    const std::filesystem::path file = temp.path() / "intent-only.bcad";
    REQUIRE(io::saveDocument(study.document, file).has_value());
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const auto& after = onlyAnalysis(*loaded).definition();
    CHECK(after.loads.size() == 3U);
    CHECK(after.boundaryConditions.size() == 2U);
    CHECK(after.solver.relativeResidualTolerance == kTolerance);
    CHECK(after.mode == ThermalAnalysisMode::Transient);
}

// ---------------------------------------------------------------------------
// Load does not solve; destroy, load, re-solve
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_ALoadedAnalysisHasIntentAndNoDerivedState", "[thermal][io][persist][resolve]") {
    TempDir temp;
    VizStudy study;
    vztest::define(study, transientGolden(study));
    const std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
    REQUIRE(derived->post.has_value());
    REQUIRE(io::saveDocument(study.document, temp.path() / "solved.bcad").has_value());

    Result<Document> loaded = io::loadDocument(temp.path() / "solved.bcad");
    REQUIRE(loaded.has_value());
    features::Regenerator regenerator;
    requireReport(regenerator, *loaded);
    // A FRESH MESHER THAT HAS GENERATED NOTHING. The original was solved, so if anything about
    // the result had been persisted this is where it would appear.
    meshing::Mesher mesher;
    const ThermalAnalysisId id = onlyAnalysis(*loaded).analysisId();
    const MeshControlId control = onlyAnalysis(*loaded).definition().mesh;
    CHECK(mesher.mesh(control) == nullptr);   // loading generated no mesh
    CHECK_FALSE(thermal::requireThermalModel(*loaded, regenerator, mesher, control).has_value());
    const thermal::ThermalDerivedState nothing{};
    const thermal::ThermalValidationReport report = thermal::validateThermalAnalysis(*loaded, regenerator, mesher, id, nothing);
    CHECK(report.status() == thermal::ValidationStatus::Fail);
    // A result held from BEFORE the save does not become current for the loaded document merely
    // because the identifiers match: its source names a mesh stamp the fresh mesher never made.
    const renderer::ThermalDisplayState display = renderer::thermalDisplayState(
        *loaded, regenerator, mesher, id, &*derived->post, nullptr, nullptr);
    CHECK(display.result != renderer::ThermalResultState::Current);
}

TEST_CASE("ThermalPersistence_ASteadyDestroyAndLoadRoundTripReSolvesEquivalently",
          "[thermal][io][persist][resolve]") {
    // THE CENTRAL QUALIFICATION TEST. Create, solve, save, DESTROY every in-memory object, load
    // into a fresh document, regenerate the mesh through the normal P16 path and re-solve through
    // the normal pipeline -- then compare the engineering results. Nothing is carried across but
    // the file.
    TempDir temp;
    const std::filesystem::path file = temp.path() / "resolve-steady.bcad";
    Semantics original;
    std::string originalIntent;
    {
        VizStudy study;
        vztest::define(study, steadyGolden(study));
        original = semanticsOf(study);
        originalIntent = fingerprint(onlyAnalysis(study.document));
        REQUIRE(io::saveDocument(study.document, file).has_value());
    }
    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    CHECK(fingerprint(onlyAnalysis(*loaded)) == originalIntent);   // intent first, before anything is regenerated

    VizStudy again;
    adopt(again, std::move(*loaded));
    const Semantics reSolved = semanticsOf(again);
    requireSame(original, reSolved);
    CHECK(original.times.size() == 1U);   // one steady state
    WARN(std::format("steady: {} nodes, {} elements, validation {}", original.nodes, original.elements,
                     thermal::toString(original.status)));
}

TEST_CASE("ThermalPersistence_ATransientDestroyAndLoadRoundTripReSolvesEquivalently",
          "[thermal][io][persist][resolve]") {
    TempDir temp;
    const std::filesystem::path file = temp.path() / "resolve-transient.bcad";
    Semantics original;
    std::string originalIntent;
    {
        VizStudy study;
        vztest::define(study, transientGolden(study));
        original = semanticsOf(study);
        originalIntent = fingerprint(onlyAnalysis(study.document));
        REQUIRE(io::saveDocument(study.document, file).has_value());
    }
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    CHECK(fingerprint(onlyAnalysis(*loaded)) == originalIntent);

    VizStudy again;
    adopt(again, std::move(*loaded));
    const Semantics reSolved = semanticsOf(again);
    requireSame(original, reSolved);
    // nineteen steps plus the initial state, and the stored times are the grid's, not n * dt
    REQUIRE(original.times.size() == 20U);
    CHECK(original.times.front() == 0.0);
    CHECK(original.times.back() == kEnd);
    WARN(std::format("transient: {} states, final time {} s, validation {}", original.times.size(),
                     original.times.back(), thermal::toString(original.status)));
}

TEST_CASE("ThermalPersistence_ATransientWithAShortenedLastStepReSolvesEquivalently",
          "[thermal][io][persist][resolve]") {
    // 0.3 does not divide 1.0: the grid takes 0.3, 0.3, 0.3 and a shortened remainder, and lands
    // on the end time exactly. The stored times come from the grid, never from n * dt, and the
    // canonical settings carry only the step, the end time and the policy.
    TempDir temp;
    const std::filesystem::path file = temp.path() / "resolve-partial.bcad";
    Semantics original;
    {
        VizStudy study;
        ThermalAnalysisDefinition definition = transientGolden(study);
        definition.transient = TransientSettings{.timeStep = Time::fromSi(0.3), .endTime = Time::fromSi(1.0),
                                                 .output = TransientOutputPolicy::EveryStep};
        vztest::define(study, definition);
        original = semanticsOf(study);
        REQUIRE(io::saveDocument(study.document, file).has_value());
    }
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    CHECK(onlyAnalysis(*loaded).definition().transient->timeStep.si() == 0.3);
    VizStudy again;
    adopt(again, std::move(*loaded));
    requireSame(original, semanticsOf(again));
    REQUIRE(original.times.size() == 5U);
    CHECK(original.times.back() == 1.0);
}

TEST_CASE("ThermalPersistence_AReloadedFaceLoadResolvesToTheSameGeometry", "[thermal][io][persist][resolve][reference]") {
    // The reference is canonical, the facets are derived. After a save and a load, the same
    // FaceName selects the same part of the same boundary: the same prepared facets in the same
    // cardinality and the same resultant heat, compared semantically and not by mesh identity.
    TempDir temp;
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::unique_ptr<vztest::Derived> before = vztest::derive(study);
    REQUIRE(before->loads.has_value());
    REQUIRE(before->balance.has_value());

    VizStudy again;
    adopt(again, roundTrip(study.document, temp.path() / "faces-resolve.bcad"));
    const std::unique_ptr<vztest::Derived> after = vztest::derive(again);
    REQUIRE(after->loads.has_value());
    REQUIRE(after->balance.has_value());

    const thermal::ThermalHeatBalance& a = before->balance->finalState();
    const thermal::ThermalHeatBalance& b = after->balance->finalState();
    REQUIRE(a.surfaceLoads.size() == b.surfaceLoads.size());
    REQUIRE(a.surfaceLoads.size() >= 1U);
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), Catch::Matchers::WithinRel(a.terms.appliedSurfaceHeat.si(), 1e-12));
    CHECK_THAT(b.terms.generatedHeat.si(), Catch::Matchers::WithinRel(a.terms.generatedHeat.si(), 1e-12));
    CHECK_THAT(b.terms.convectionHeat.si(), Catch::Matchers::WithinRel(a.terms.convectionHeat.si(), 1e-12));
    // The applied heat is flux x area, and the area comes from the face the reference selects:
    // -1234.5 W/m^2 over side face 1 (30 mm x 20 mm = 6e-4 m^2 -> a -0.7407 W term), a hand value.
    CHECK_THAT(a.terms.appliedSurfaceHeat.si(), Catch::Matchers::WithinRel(-1234.5 * 6.0e-4, 1e-9));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), Catch::Matchers::WithinRel(-1234.5 * 6.0e-4, 1e-9));
}

TEST_CASE("ThermalPersistence_ALoadedDocumentKeepsItsIdentitiesAndCommandsStillWork", "[thermal][io][persist][commands]") {
    // Ids that came from a file are as good as ids made in this session: a command cannot reuse
    // one, can use an unused one, and undoes and redoes exactly against a loaded analysis.
    TempDir temp;
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    Document loaded = roundTrip(study.document, temp.path() / "ids.bcad");
    const ThermalAnalysisId id = onlyAnalysis(loaded).analysisId();
    CHECK(id == study.analysis);
    REQUIRE(onlyAnalysis(loaded).definition().loads.size() == 3U);
    CHECK(onlyAnalysis(loaded).definition().loads[0].id().value() == 4U);

    CommandHistory history;
    CHECK_FALSE(history.execute(loaded, std::make_unique<thermal::AddThermalLoadCommand>(id, vztest::generationLoad(4, 1.0))).has_value());
    const std::string before = fingerprint(onlyAnalysis(loaded));
    REQUIRE(history.execute(loaded, std::make_unique<thermal::AddThermalLoadCommand>(id, vztest::generationLoad(9, 1.0))).has_value());
    CHECK(onlyAnalysis(loaded).definition().loads.size() == 4U);
    REQUIRE(history.undo(loaded).has_value());
    CHECK(fingerprint(onlyAnalysis(loaded)) == before);
    REQUIRE(history.redo(loaded).has_value());
    CHECK(onlyAnalysis(loaded).definition().loads.size() == 4U);
    // and the command history does not reach the file
    CHECK(history.undoCount() == 1U);
}

TEST_CASE("ThermalPersistence_AResultOfOneDocumentDoesNotAttachToACopyThatDiffers",
          "[thermal][io][persist][resolve][isolation]") {
    // Two documents loaded from one file hold the same ThermalLoadIds, BoundaryConditionIds and
    // object ids. A result held for one must not attach to the other BY IDENTIFIER: it is current
    // for a document only while the whole recorded source -- mesh stamp, geometry, material and
    // the analysis revision -- is. The copy is edited until its analysis REVISION equals the
    // original's, the case in which a revision-only comparison would alias, and its content
    // differs; the original's result must still be stale for it, and say WHY.
    TempDir temp;
    VizStudy original;
    vztest::define(original, steadyGolden(original));
    const std::unique_ptr<vztest::Derived> derived = vztest::derive(original);
    REQUIRE(derived->post.has_value());
    const thermal::ThermalValidationReport report = vztest::validationOf(original, *derived);
    const std::filesystem::path file = temp.path() / "copy.bcad";
    REQUIRE(io::saveDocument(original.document, file).has_value());

    VizStudy copy;
    {
        Result<Document> loaded = io::loadDocument(file);
        REQUIRE(loaded.has_value());
        adopt(copy, std::move(*loaded));
    }
    REQUIRE(copy.analysis == original.analysis);   // the same identifiers
    const renderer::ThermalDisplayState sameDocument = vztest::displayStateOf(original, derived.get(), &report);
    CHECK(sameDocument.result == renderer::ThermalResultState::Current);

    // The UNEDITED copy: same content, same identifiers, and still not the original's. A result is
    // bound to the mesh stamp of the mesher that made it, and the copy's mesh is its own, so the
    // only thing that differs is the mesh -- and that alone is enough to make it stale.
    const renderer::ThermalDisplayState untouched = vztest::displayStateOf(copy, derived.get(), nullptr);
    CHECK(untouched.result != renderer::ThermalResultState::Current);
    REQUIRE(untouched.staleReasons.size() == 1U);
    CHECK(untouched.staleReasons.front() == thermal::TransientStaleReason::Mesh);
    WARN(std::format("analysis revision {} (original) vs {} (a fresh load)", onlyAnalysis(original.document).revision(),
                     onlyAnalysis(copy.document).revision()));

    // Edit the copy through the command layer until its revision equals the original's.
    const std::uint64_t target = onlyAnalysis(original.document).revision();
    CommandHistory history;
    double generation = 2.0;
    REQUIRE(history.execute(copy.document, std::make_unique<thermal::EditThermalLoadCommand>(copy.analysis, vztest::generationLoad(2, generation))).has_value());
    while (onlyAnalysis(copy.document).revision() < target) {
        generation += 1.0;
        REQUIRE(history.execute(copy.document, std::make_unique<thermal::EditThermalLoadCommand>(copy.analysis, vztest::generationLoad(2, generation))).has_value());
    }
    WARN(std::format("analysis revisions: original {} | copy {} after {} edits", target,
                     onlyAnalysis(copy.document).revision(), history.undoCount()));
    CHECK(onlyAnalysis(copy.document).definition() != onlyAnalysis(original.document).definition());
    const renderer::ThermalDisplayState onTheCopy = vztest::displayStateOf(copy, derived.get(), nullptr);
    CHECK(onTheCopy.result != renderer::ThermalResultState::Current);
    CHECK_FALSE(onTheCopy.staleReasons.empty());
    // WHY: the mesh (a different document's mesh) AND the loads -- the derived chain compares the
    // canonical intent by VALUE, so the equal analysis revisions that were arranged above alias
    // nothing.
    const auto has = [&](thermal::TransientStaleReason reason) {
        return std::ranges::find(onTheCopy.staleReasons, reason) != onTheCopy.staleReasons.end();
    };
    CHECK(has(thermal::TransientStaleReason::Mesh));
    CHECK(has(thermal::TransientStaleReason::Loads));
}
TEST_CASE("ThermalPersistence_ObjectNamesRoundTripExactlyIncludingUnicodeAndEscapes",
          "[thermal][io][persist][encoding]") {
    // The object's name is the only free text a thermal analysis carries. Every name the core
    // accepts must come back unchanged; a name it refuses is refused identically each time.
    TempDir temp;
    std::size_t accepted = 0U;
    for (const char* name : {"Study2", "Thermal \xCE\xB1", "A \"quoted\" \\ name", "plate thermal 2", "caf\xC3\xA9"}) {
        INFO(name);
        VizStudy study;
        auto analysis = ThermalAnalysis::create(name, steadyGolden(study));
        if (!analysis.has_value()) {
            CHECK_FALSE(ThermalAnalysis::create(name, steadyGolden(study)).has_value());
            continue;
        }
        const Result<ObjectId> added = study.document.addObject(std::move(*analysis));
        if (!added.has_value()) {
            continue;   // the document refuses this name; nothing to round trip
        }
        ++accepted;
        const ObjectId id = *added;
        const Document loaded = roundTrip(study.document, temp.path() / "name.bcad");
        const DocumentObject* found = loaded.findObject(id);
        REQUIRE(found != nullptr);
        CHECK(found->name() == name);
    }
    CHECK(accepted >= 1U);
    WARN(std::format("{} of 5 object names are valid and round-trip exactly", accepted));
}

TEST_CASE("ThermalPersistence_AThermalAndAStructuralAnalysisShareOneFileWithoutInterfering",
          "[thermal][io][persist][shared-format]") {
    // The format has one object-dispatch; a second analysis type must not disturb the first.
    TempDir temp;
    VizStudy study;
    vztest::define(study, transientGolden(study));
    auto structuralAnalysis = structural::StructuralAnalysis::create(
        "Structural", structural::StructuralAnalysisDefinition{
                          .mesh = study.control,
                          .loads = {structural::StructuralLoad{
                              LoadId::fromValue(1U),
                              structural::PressureLoad{.face = study.endCap(), .magnitude = Pressure::fromSi(2.5e6)}}},
                          .restraints = {structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1U),
                                                                                       study.startCap())}});
    REQUIRE(structuralAnalysis.has_value());
    const ObjectId structuralId = require(study.document.addObject(std::move(*structuralAnalysis)));

    const Document loaded = roundTrip(study.document, temp.path() / "both.bcad");
    CHECK(onlyAnalysis(loaded).definition() == onlyAnalysis(study.document).definition());
    const structural::StructuralAnalysis* structuralBack =
        structural::findStructuralAnalysis(loaded, AnalysisId::fromValue(structuralId.value()));
    REQUIRE(structuralBack != nullptr);
    CHECK(structuralBack->definition().loads.size() == 1U);
    CHECK(structuralBack->definition() ==
          structural::findStructuralAnalysis(study.document, AnalysisId::fromValue(structuralId.value()))->definition());
    // and the thermal bytes do not depend on the presence of the other analysis
    VizStudy alone;
    vztest::define(alone, transientGolden(alone));
    const std::string withBoth = saveToString(study.document);
    const std::size_t at = withBoth.find(kMarker);
    const std::size_t aloneAt = saveToString(alone.document).find(kMarker);
    REQUIRE(at != std::string::npos);
    REQUIRE(aloneAt != std::string::npos);
    const std::string thermalOnly = saveToString(alone.document).substr(aloneAt);
    // the WHOLE thermal object, from its marker to the end of that object, in both files
    const std::size_t cutBoth = withBoth.find("\n    },\n    {", at);
    REQUIRE(cutBoth != std::string::npos);
    const std::size_t cutAlone = thermalOnly.find("\n    }\n  ]");
    REQUIRE(cutAlone != std::string::npos);
    CHECK(withBoth.substr(at, cutBoth - at) == thermalOnly.substr(0, cutAlone));
    CHECK(cutAlone > 1000U);
}
TEST_CASE("ThermalPersistence_AnIdentifierAboveTheFileCapIsRefusedOnSaveRatherThanWrittenAndLostOnLoad",
          "[thermal][io][persist][malformed][ids]") {
    // `validate` asks only that an identity be VALID, and a file identifier is capped at 2^53 - 1
    // (the largest integer every JSON implementation represents exactly). An identity above the
    // cap used to be writable and then unreadable. The writer refuses it, by name, and writes no
    // file; the largest permitted identity is written and read back.
    TempDir temp;
    constexpr std::uint64_t kCap = (std::uint64_t{1} << 53) - 1U;
    VizStudy study;
    ThermalAnalysisDefinition definition = steadyGolden(study);

    SECTION("a load id one above the cap") {
        definition.loads[0] = ThermalLoad{ThermalLoadId::fromValue(kCap + 1U),
                                          VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(1.0)}};
        vztest::define(study, definition);
        const Result<std::string> written = io::documentToJson(study.document);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(written.error().message, ContainsSubstring("the load id 9007199254740992 exceeds"));
        const std::filesystem::path file = temp.path() / "refused.bcad";
        CHECK_FALSE(io::saveDocument(study.document, file).has_value());
        CHECK_FALSE(std::filesystem::exists(file));
    }
    SECTION("a condition id far above the cap") {
        definition.boundaryConditions[0] = vztest::temperatureBc(std::uint64_t{1} << 60, study.startCap(), 300.0);
        vztest::define(study, definition);
        const Result<std::string> written = io::documentToJson(study.document);
        REQUIRE_FALSE(written.has_value());
        CHECK_THAT(written.error().message, ContainsSubstring("the boundary condition id"));
    }
    SECTION("the largest permitted id is written and read back") {
        definition.loads[0] = ThermalLoad{ThermalLoadId::fromValue(kCap),
                                          VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(1.0)}};
        vztest::define(study, definition);
        const Document loaded = roundTrip(study.document, temp.path() / "cap.bcad");
        CHECK(onlyAnalysis(loaded).definition().loads[0].id().value() == kCap);
    }
}

TEST_CASE("ThermalPersistence_ARepeatedKeyInTheFileIsLastWinsAsEverywhereInTheFormat",
          "[thermal][io][persist][malformed][format-wide]") {
    // MEASURED, AND FORMAT-WIDE. The JSON parser collapses a repeated key before any reader sees
    // it, so `{"flux": 1, "flux": -1}` loads as -1 for a thermal load exactly as a repeated key
    // does for every other object type. Making the parser strict would change how every
    // document in the format loads and is a decision for the owner, not for one object type's
    // persistence; this test pins the behaviour so that it is recorded and cannot change
    // silently. (Duplicate IDENTITIES -- the brief's rule -- are refused; see the duplicate test.)
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    const std::string end = faceJson(study, "end_cap");
    const std::string text = withAnalysisData(
        good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "surface_heat_flux", "face": {}, "flux": 1.0, "flux": -1.0}}], {})", end, kSolver)));
    Result<Document> loaded = io::documentFromJson(text);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    CHECK(onlyAnalysis(*loaded).definition().loads[0].surfaceHeatFlux()->flux.si() == -1.0);
    // and the file this build would write for it has the key once
    const std::string resaved = analysisSectionOf(saveToString(*loaded));
    CHECK(resaved.find("\"flux\": -1.0") != std::string::npos);
    CHECK(resaved.find("\"flux\"") == resaved.rfind("\"flux\""));
}

TEST_CASE("ThermalPersistence_DanglingTargetsAreIntentAndAnInvalidFeatureHandleIsNot",
          "[thermal][io][persist][reference]") {
    VizStudy study;
    vztest::define(study, steadyGolden(study));
    const std::string good = saveToString(study.document);
    const std::string end = faceJson(study, "end_cap");

    SECTION("a mesh control that does not exist loads: the reference is preserved, the chain reports it") {
        Result<Document> loaded = io::documentFromJson(withAnalysisData(
            good, std::format(R"({{"mesh": 424242, "mode": "steady", {}}})", kSolver)));
        INFO((loaded ? std::string{} : loaded.error().message));
        REQUIRE(loaded.has_value());
        const ThermalAnalysis& analysis = onlyAnalysis(*loaded);
        CHECK(analysis.definition().mesh.value() == 424242U);
        CHECK(loaded->findObject(ObjectId::fromValue(424242U)) == nullptr);
        // the derived chain, not the file, refuses it
        features::Regenerator regenerator;
        requireReport(regenerator, *loaded);
        meshing::Mesher mesher;
        CHECK_FALSE(thermal::requireThermalModel(*loaded, regenerator, mesher, analysis.definition().mesh).has_value());
    }
    SECTION("a mesh control that is an object of another kind loads too") {
        Result<Document> loaded = io::documentFromJson(withAnalysisData(
            good, std::format(R"({{"mesh": {}, "mode": "steady", {}}})", study.feature.value(), kSolver)));
        REQUIRE(loaded.has_value());
        features::Regenerator regenerator;
        requireReport(regenerator, *loaded);
        meshing::Mesher mesher;
        CHECK_FALSE(thermal::requireThermalModel(*loaded, regenerator, mesher, onlyAnalysis(*loaded).definition().mesh).has_value());
    }
    SECTION("a face of a feature handle that names no feature loads as an unresolved target") {
        Result<Document> loaded = io::documentFromJson(withAnalysisData(
            good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "surface_heat_flux", "face": {{"feature": 555555, "face": {{"role": "end_cap"}}}}, "flux": 5.0}}], {})", kSolver))));
        REQUIRE(loaded.has_value());
        CHECK(onlyAnalysis(*loaded).definition().loads[0].surfaceHeatFlux()->face.feature.value() == 555555U);
    }
    SECTION("the INVALID feature handle is refused: it is not a face that fails to resolve but a record that names nothing") {
        for (const char* feature : {"0"}) {
            INFO(feature);
            const Error load = loadFailure(withAnalysisData(
                good, steadyData(study, std::format(R"("loads": [{{"id": 1, "type": "surface_heat_flux", "face": {{"feature": {}, "face": {{"role": "end_cap"}}}}, "flux": 5.0}}], {})", feature, kSolver))));
            CHECK_THAT(load.message, ContainsSubstring(".loads[0].face.feature"));
            CHECK_THAT(load.message, ContainsSubstring("a target must name a feature"));
            const Error condition = loadFailure(withAnalysisData(
                good, steadyData(study, std::format(R"("boundary_conditions": [{{"id": 1, "type": "prescribed_temperature", "face": {{"feature": {}, "face": {{"role": "end_cap"}}}}, "temperature": 300.0}}], {})", feature, kSolver))));
            CHECK_THAT(condition.message, ContainsSubstring(".boundary_conditions[0].face.feature"));
        }
    }
}

TEST_CASE("ThermalPersistence_TheHandWrittenFileIsExactlyWhatTheWriterProduces",
          "[thermal][io][persist][bytes][reference-file]") {
    // THE GOLDEN BYTES. Every other byte test compares the writer with itself in one process,
    // which cannot see a changed key order, indentation or number format. The reference file was
    // typed by hand, in the order and the formatting this schema defines, so loading it and
    // saving it again must reproduce its thermal section character for character -- on every
    // preset and every build, which is the cross-preset byte claim in its strongest form.
    const std::filesystem::path file = std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "thermal_plate.bcad";
    const std::string handWritten = readFile(file);
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    const std::string written = saveToString(*loaded);
    {
        const std::string a = analysisSectionOf(written);
        const std::string b = analysisSectionOf(handWritten);
        const auto [x, y] = std::mismatch(a.begin(), a.end(), b.begin(), b.end());
        INFO(std::format("sections: first difference at offset {} of {} / {} bytes: writer '{}' file '{}'", x - a.begin(),
                         a.size(), b.size(), x == a.end() ? std::string{"<end>"} : std::string(x, std::min(x + 30, a.end())),
                         y == b.end() ? std::string{"<end>"} : std::string(y, std::min(y + 30, b.end()))));
        CHECK(a == b);
    }
    // the whole file as well, apart from the header's document id which a Document keeps from the file
    {
        const auto [x, y] = std::mismatch(written.begin(), written.end(), handWritten.begin(), handWritten.end());
        INFO(std::format("whole file: first difference at offset {} of {} / {} bytes: writer '{}' file '{}'", x - written.begin(),
                         written.size(), handWritten.size(),
                         x == written.end() ? std::string{"<end>"} : std::string(x, std::min(x + 40, written.end())),
                         y == handWritten.end() ? std::string{"<end>"} : std::string(y, std::min(y + 40, handWritten.end()))));
        CHECK(written == handWritten);
    }
}

TEST_CASE("ThermalPersistence_TheGoldenSectionsHaveFixedBytes", "[thermal][io][persist][bytes]") {
    // A fingerprint of the exact bytes of the steady and transient golden sections, typed in
    // here. It is the same in debug, release and shared-library builds by construction of the
    // test, and it changes only when the schema, the key order, the formatting or a golden value
    // does -- which is then a deliberate act with a new constant, not a silent drift.
    const auto fnv = [](const std::string& text) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const char c : text) {
            hash = (hash ^ static_cast<std::uint64_t>(static_cast<unsigned char>(c))) * 1099511628211ULL;
        }
        return hash;
    };
    VizStudy steady;
    vztest::define(steady, steadyGolden(steady));
    VizStudy transient;
    vztest::define(transient, transientGolden(transient));
    const std::string steadyText = analysisSectionOf(saveToString(steady.document));
    const std::string transientText = analysisSectionOf(saveToString(transient.document));
    WARN(std::format("PERFP steady {:016x} {} bytes | transient {:016x} {} bytes", fnv(steadyText),
                     steadyText.size(), fnv(transientText), transientText.size()));
    CHECK(fnv(steadyText) == 0x4cec5ef3f4cc7014ULL);
    CHECK(fnv(transientText) == 0x23d057e01f95e19cULL);
}
// ---------------------------------------------------------------------------
// The hand-written reference file
// ---------------------------------------------------------------------------

TEST_CASE("ThermalPersistence_TheHandWrittenReferenceFileLoadsAsWritten", "[thermal][io][persist][reference-file]") {
    // examples/models/thermal_plate.bcad was written by hand, not by the serializer, so a
    // misunderstanding shared by the writer and the reader cannot hide in it: every number below
    // is typed from the file.
    const std::filesystem::path file = std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "thermal_plate.bcad";
    REQUIRE(std::filesystem::exists(file));
    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    const ThermalAnalysis& analysis = onlyAnalysis(*loaded);
    const auto& d = analysis.definition();
    CHECK(analysis.name() == "PlateThermalStudy");
    CHECK(d.mode == ThermalAnalysisMode::Transient);
    REQUIRE(d.loads.size() == 2U);
    CHECK(d.loads[0].id().value() == 1U);
    REQUIRE(d.loads[0].surfaceHeatFlux() != nullptr);
    CHECK(d.loads[0].surfaceHeatFlux()->flux.si() == -1234.5);
    CHECK(d.loads[0].surfaceHeatFlux()->face.face.role == FaceRole::EndCap);
    REQUIRE(d.loads[1].volumetricHeatSource() != nullptr);
    CHECK(d.loads[1].volumetricHeatSource()->generation.si() == 67890.25);
    REQUIRE(d.boundaryConditions.size() == 2U);
    CHECK(d.boundaryConditions[0].prescribedTemperature()->value.si() == 345.125);
    CHECK(d.boundaryConditions[1].convection()->coefficient.si() == 27.75);
    CHECK(d.boundaryConditions[1].convection()->ambient.si() == 291.625);
    CHECK(d.initialCondition->value.si() == 312.875);
    CHECK(d.transient->timeStep.si() == 0.125);
    CHECK(d.transient->endTime.si() == 2.375);
    CHECK(d.transient->output == TransientOutputPolicy::EveryStep);
    CHECK(d.solver.relativeResidualTolerance == 1.0e-8);
    CHECK(d.solver.pivotFloor == 1.0e-12);
}

TEST_CASE("ThermalPersistence_TheReferenceFileRegeneratesMeshesAndSolves", "[thermal][io][persist][reference-file][resolve]") {
    // The hand-written file is not merely readable: it describes a model that meshes, solves,
    // recovers, balances and validates from the file alone -- the whole chain, with nothing
    // carried in but the document's own canonical data (P15 material, P16 meshing control).
    const std::filesystem::path file = std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "thermal_plate.bcad";
    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    VizStudy study;
    adopt(study, std::move(*loaded));
    REQUIRE(study.analysis.value() == 9U);

    const std::unique_ptr<vztest::Derived> derived = vztest::derive(study);
    INFO(derived->stoppedBecause);
    REQUIRE(derived->loads.has_value());
    REQUIRE(derived->boundary.has_value());
    REQUIRE(derived->system.has_value());
    REQUIRE(derived->transient.has_value());
    REQUIRE(derived->post.has_value());
    REQUIRE(derived->balance.has_value());
    CHECK(derived->post->stateCount() == 20U);   // nineteen steps of 0.125 s plus the initial state
    CHECK(derived->post->states().back().time->si() == 2.375);
    const thermal::ThermalValidationReport report = vztest::validationOf(study, *derived);
    CHECK(report.status() != thermal::ValidationStatus::Fail);
    WARN(std::format("reference file: {} nodes, {} elements, {} states, validation {}",
                     study.mesh().nodeCount(), study.mesh().elementCount(), derived->post->stateCount(),
                     thermal::toString(report.status())));
}
