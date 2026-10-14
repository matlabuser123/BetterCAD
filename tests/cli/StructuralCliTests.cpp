// P17-CLI-001: headless structural FEA.
//
// WHAT THESE TESTS ARE FOR. The commands are adapters, so the thing worth
// proving is not that they print something but that they print the SAME thing
// the core would say, that they fail exactly where the core fails, and that
// the exit status carries the gate. A test that only checked "exit 0 and some
// words" would pass against a CLI that had quietly grown its own von Mises
// formula or its own restraint-count rule.
//
// SO EVERY ENGINEERING ASSERTION HERE IS AN EQUIVALENCE. The core is run in
// process, its result is read through its own accessors, and the number the
// CLI printed is compared against it. Nothing is compared against a literal
// that a change to the physics could not move.
//
// For the stronger form -- the real executable in separate processes -- see the
// process tests registered in tests/CMakeLists.txt (cli.structural.*) and the
// multi-process script in StructuralWorkflow.cmake, which is where argument
// parsing, document loading, runtime deployment, the exit code and stdout
// cleanliness are actually exercised.

#include "CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralCommands.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralReaction.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>
#include <bettercad/structural/StructuralValidation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using structural::DofComponent;
using structural::RestraintComponents;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralLoad;
using structural::StructuralRestraint;

namespace {

constexpr double kYoungs = 2.1e11;
constexpr double kPoisson = 0.3;
constexpr double kPressure = 2.5e6;

/// The fixture document: a 40 x 30 x 20 block, steel, one meshing control, one
/// structural analysis with a pressure on the end cap and a fixed start cap.
///
/// Built through the production APIs and saved, so every CLI run below starts
/// from a real file. Asymmetric, so no two faces are interchangeable.
struct Part {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    std::array<EntityId, 4> lines{};

    explicit Part(bool withMaterial = true, bool withRestraint = true) {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*profile, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh", meshing::MeshControlDefinition{
                        .body = feature, .mesh = {.sizing = {.globalTargetSize = 20_mm}}});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        if (withMaterial) {
            features::MaterialDefinition material;
            material.designation = "Steel";
            material.mechanical.youngsModulus =
                materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
            material.mechanical.poissonRatio =
                materials::MaterialProperty<bettercad::PoissonRatio>::known(
                    PoissonRatio::of(kPoisson));
            const Result<MaterialId> id = features::createMaterial(document, "Steel", material);
            REQUIRE(id.has_value());
            REQUIRE(features::assignMaterial(document, *id).has_value());
        }

        StructuralAnalysisDefinition definition;
        definition.mesh = control;
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(1U),
            structural::PressureLoad{.face = endCap(),
                                     .magnitude = Pressure::fromSi(kPressure)}});
        if (withRestraint) {
            definition.restraints.push_back(
                StructuralRestraint::fixedSupport(RestraintId::fromValue(1U), startCap()));
        }
        auto study = StructuralAnalysis::create("Study", definition);
        INFO((study ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines[which]}};
    }
    /// Well formed and names an entity no face is generated by: unresolved,
    /// not malformed.
    [[nodiscard]] FaceName unresolvable() const {
        return FaceName{feature,
                        FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}};
    }
    /// The command-line spelling of a face, the one the CLI accepts.
    [[nodiscard]] static std::string text(std::string_view role) {
        return std::format("face:Solid:{}", role);
    }
    [[nodiscard]] std::string sideText(std::size_t which) const {
        return std::format("face:Solid:side:{}", lines[which].value());
    }

    [[nodiscard]] std::filesystem::path save(const TempDir& temp, std::string_view name) const {
        const std::filesystem::path file = temp.path() / std::format("{}.bcad", name);
        const Result<void> saved = io::saveDocument(document, file);
        INFO((saved ? std::string{} : saved.error().message));
        REQUIRE(saved.has_value());
        return file;
    }

    /// The core's own answer, through the production entry, for the CLI to be
    /// compared against.
    [[nodiscard]] Result<structural::StructuralSolveOutcome> solveInCore() {
        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
        return structural::solveStructuralAnalysis(document, regenerator, mesher, analysis);
    }

    void setAnalysis(StructuralAnalysisDefinition definition) {
        const Result<bool> changed = document.modifyObject<StructuralAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&](StructuralAnalysis& object) -> Result<bool> {
                return object.setDefinition(std::move(definition));
            });
        INFO((changed ? std::string{} : changed.error().message));
        REQUIRE(changed.has_value());
    }

    [[nodiscard]] StructuralAnalysisDefinition definition() const {
        const StructuralAnalysis* found = structural::findStructuralAnalysis(document, analysis);
        REQUIRE(found != nullptr);
        return found->definition();
    }
};

/// The number a JSON field holds, by key, from the CLI's own output.
///
/// A MINIMAL READER AND NOT A PARSER. The whole-payload parse gate is the
/// workflow script's, which uses CMake's `string(JSON)` -- a real parser that
/// refuses malformed input. This reads one value from output already known to
/// be well formed, so that an equivalence test can compare a number rather
/// than a substring.
[[nodiscard]] double jsonNumber(const std::string& payload, std::string_view key) {
    const std::string needle = std::format("\"{}\": ", key);
    const std::size_t at = payload.find(needle);
    INFO(std::format("key '{}' in:\n{}", key, payload));
    REQUIRE(at != std::string::npos);
    const std::size_t from = at + needle.size();
    const std::size_t to = payload.find_first_of(",\n}", from);
    REQUIRE(to != std::string::npos);
    return std::stod(payload.substr(from, to - from));
}

/// The `value` inside a `{"value": ..., "unit": ...}` quantity, by its key.
///
/// NEEDED BECAUSE A QUANTITY IS NESTED, which is the point of it: the CLI
/// never writes a bare engineering number. Reading one with the flat reader
/// above returned the opening brace and threw, which is how this helper came
/// to exist rather than by foresight.
[[nodiscard]] double jsonQuantity(const std::string& payload, std::string_view key) {
    const std::string needle = std::format("\"{}\": {{", key);
    const std::size_t at = payload.find(needle);
    INFO(std::format("quantity '{}' in:\n{}", key, payload));
    REQUIRE(at != std::string::npos);
    const std::size_t valueAt = payload.find("\"value\": ", at);
    REQUIRE(valueAt != std::string::npos);
    const std::size_t from = valueAt + std::string_view{"\"value\": "}.size();
    const std::size_t to = payload.find_first_of(",\n}", from);
    REQUIRE(to != std::string::npos);
    return std::stod(payload.substr(from, to - from));
}

[[nodiscard]] std::string jsonText(const std::string& payload, std::string_view key) {
    const std::string needle = std::format("\"{}\": \"", key);
    const std::size_t at = payload.find(needle);
    INFO(std::format("key '{}' in:\n{}", key, payload));
    REQUIRE(at != std::string::npos);
    const std::size_t from = at + needle.size();
    const std::size_t to = payload.find('"', from);
    REQUIRE(to != std::string::npos);
    return payload.substr(from, to - from);
}

/// The text of one nested object, so a reader can be scoped to it.
///
/// NEEDED BECAUSE A KEY REPEATS. The stress payload carries `"vonMises"`
/// twice -- once as a range and once inside `largest` -- so an unscoped search
/// finds whichever comes first. Probe L5 survived because of exactly that: it
/// corrupted the value inside `largest`, which nothing was reading.
/// Handles an object and an ARRAY alike, because `loads` and `restraints` are
/// arrays while `largest` and `components` are objects, and a reader that knew
/// only one of the two would be useful for half the payload.
[[nodiscard]] std::string jsonSection(const std::string& payload, std::string_view key) {
    const std::string needle = std::format("\"{}\": ", key);
    const std::size_t at = payload.find(needle);
    INFO(std::format("section '{}' in:\n{}", key, payload));
    REQUIRE(at != std::string::npos);
    const std::size_t open = at + needle.size();
    REQUIRE(open < payload.size());
    const char opener = payload[open];
    REQUIRE((opener == '{' || opener == '['));
    const char closer = opener == '{' ? '}' : ']';
    std::size_t depth = 0;
    for (std::size_t i = open; i < payload.size(); ++i) {
        if (payload[i] == opener) {
            ++depth;
        } else if (payload[i] == closer) {
            --depth;
            if (depth == 0U) {
                return payload.substr(open, i - open + 1U);
            }
        }
    }
    FAIL("unterminated section");
    return {};
}

/// Canonical structural intent, read from a document on disk.
///
/// The same shape P17-PERSIST-001's tests use: the mesh control, the mode,
/// the loads in order with their targets and values at full precision, the
/// restraints in order with their masks, and the solver settings. It reads no
/// revision, no mesh and no result, so two documents compare equal only when
/// their canonical intent really is equal.
[[nodiscard]] std::string fingerprintOf(const std::filesystem::path& file) {
    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    const std::vector<AnalysisId> ids = structural::structuralAnalysisIds(*loaded);
    REQUIRE(ids.size() == 1U);
    const StructuralAnalysis* analysis = structural::findStructuralAnalysis(*loaded, ids.front());
    REQUIRE(analysis != nullptr);
    const StructuralAnalysisDefinition& d = analysis->definition();

    std::string text = std::format("mesh={}|mode={}", d.mesh.value(), toString(d.mode));
    for (const StructuralLoad& load : d.loads) {
        text += std::format("|load[{}:{}]", load.id().value(), toString(load.kind()));
        if (const std::optional<FaceName> target = load.target()) {
            text += std::format("({},{})", target->feature.value(),
                                static_cast<int>(target->face.role));
            if (target->face.entity) {
                text += std::format(":{}", target->face.entity->value());
            }
        }
        if (const structural::PressureLoad* p = load.pressure()) {
            text += std::format("={:.17g}", p->magnitude.si());
        } else if (const structural::SurfaceTractionLoad* t = load.traction()) {
            text += std::format("=({:.17g},{:.17g},{:.17g})", t->traction.x.si(),
                                t->traction.y.si(), t->traction.z.si());
        } else if (const structural::GravityLoad* g = load.gravity()) {
            text += std::format("=({:.17g},{:.17g},{:.17g})", g->acceleration.x,
                                g->acceleration.y, g->acceleration.z);
        }
    }
    for (const StructuralRestraint& restraint : d.restraints) {
        text += std::format("|restraint[{}]({},{}", restraint.id().value(),
                            restraint.face().feature.value(),
                            static_cast<int>(restraint.face().face.role));
        if (restraint.face().face.entity) {
            text += std::format(":{}", restraint.face().face.entity->value());
        }
        text += std::format(")={}", toString(restraint.components()));
    }
    text += std::format("|solver={},{:.17g},{:.17g}", static_cast<int>(d.solver.algorithm),
                        d.solver.relativeResidualTolerance, d.solver.pivotFloor);
    return text;
}

[[nodiscard]] std::string readText(const std::filesystem::path& file) { return readFile(file); }

} // namespace

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCli_InspectsTheCanonicalAnalysisAndNothingDerived",
          "[structural][cli]") {
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "inspect");

    const CliRun run = runCliCommand({"structural-settings", cliPath(file), "--json"});
    INFO(run.err);
    REQUIRE(run.exitCode == cli::ExitCode::Success);

    // Every canonical field, against the core's own definition.
    const StructuralAnalysisDefinition d = part.definition();
    CHECK(jsonNumber(run.out, "analysisId") ==
          static_cast<double>(part.analysis.value()));
    CHECK(jsonNumber(run.out, "meshControl") == static_cast<double>(d.mesh.value()));
    CHECK(jsonText(run.out, "mode") == std::string{toString(d.mode)});
    CHECK(jsonNumber(run.out, "loadId") == static_cast<double>(d.loads.front().id().value()));
    CHECK(jsonText(run.out, "type") == std::string{toString(d.loads.front().kind())});
    CHECK(jsonNumber(run.out, "value") == d.loads.front().pressure()->magnitude.si());
    CHECK(jsonText(run.out, "unit") == "Pa");
    CHECK(jsonNumber(run.out, "restraintId") ==
          static_cast<double>(d.restraints.front().id().value()));
    CHECK(jsonText(run.out, "components") == toString(d.restraints.front().components()));
    CHECK(jsonText(run.out, "algorithm") == std::string{toString(d.solver.algorithm)});
    CHECK(jsonNumber(run.out, "relativeResidualTolerance") ==
          d.solver.relativeResidualTolerance);
    CHECK(jsonNumber(run.out, "pivotFloor") == d.solver.pivotFloor);

    // AND NOTHING DERIVED. Inspection never meshes and never solves, so none
    // of these can be in its output -- matched as JSON keys, because
    // "restraints" contains "strain" and a bare substring search reports the
    // payload's own legitimate keys.
    for (const std::string_view forbidden :
         {"node", "element", "dof", "displacement", "strain", "stress", "vonMises",
          "reaction", "residual", "freeEquations", "nodeCount", "elementCount"}) {
        INFO(forbidden);
        CHECK(run.out.find(std::format("\"{}", forbidden)) == std::string::npos);
    }
}

TEST_CASE("StructuralCli_InspectReportsAnUnresolvedTargetRatherThanOmittingIt",
          "[structural][cli]") {
    // The brief's §5. A well-formed reference that no current face satisfies is
    // legitimate state -- the user changed the model -- and P17-PERSIST-001
    // preserves it exactly. Saying so is the whole point of inspection, and
    // dropping the entity or nulling the target would both be worse than
    // either.
    // TWO SECTIONS, EACH WITH ONLY ONE UNRESOLVED ENTITY, and that separation
    // is what probe L10 taught: the load path and the restraint path report
    // `faceResolves` from two different call sites, so a fixture carrying both
    // an unresolved RESTRAINT and resolved loads lets a mutation of the LOAD
    // site survive -- "faceResolves: false" was present either way. L10
    // hard-coded the load's to true and nothing failed.
    TempDir temp;

    SECTION("an unresolved LOAD target") {
        Part part;
        StructuralAnalysisDefinition definition = part.definition();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(8U),
            structural::PressureLoad{.face = part.unresolvable(),
                                     .magnitude = Pressure::fromSi(1.0e5)}});
        part.setAnalysis(definition);
        const std::filesystem::path file = part.save(temp, "unresolved-load");

        const CliRun run = runCliCommand({"structural-settings", cliPath(file), "--json"});
        INFO(run.err);
        REQUIRE(run.exitCode == cli::ExitCode::Success);
        // Both loads are there: nothing was dropped.
        CHECK_THAT(run.out, ContainsSubstring("\"loadId\": 1"));
        CHECK_THAT(run.out, ContainsSubstring("\"loadId\": 8"));
        CHECK_THAT(run.out, ContainsSubstring("face:Solid:side:9991"));
        // EVERY RESTRAINT RESOLVES in this fixture, so a `false` anywhere in
        // the payload can only have come from the load -- which is what makes
        // this assertion able to kill a mutation of the load's call site.
        CHECK_THAT(run.out, ContainsSubstring("\"faceResolves\": false"));
        const std::string restraints = jsonSection(run.out, "restraints");
        CHECK(restraints.find("false") == std::string::npos);

        const CliRun prose = runCliCommand({"structural-settings", cliPath(file)});
        REQUIRE(prose.exitCode == cli::ExitCode::Success);
        CHECK_THAT(prose.out, ContainsSubstring("UNRESOLVED"));
    }

    SECTION("an unresolved RESTRAINT target") {
        Part part;
        StructuralAnalysisDefinition definition = part.definition();
        definition.restraints.push_back(StructuralRestraint{RestraintId::fromValue(5U),
                                                            part.unresolvable(),
                                                            RestraintComponents::fixed()});
        part.setAnalysis(definition);
        const std::filesystem::path file = part.save(temp, "unresolved-restraint");

        const CliRun run = runCliCommand({"structural-settings", cliPath(file), "--json"});
        INFO(run.err);
        REQUIRE(run.exitCode == cli::ExitCode::Success);
        CHECK_THAT(run.out, ContainsSubstring("\"restraintId\": 1"));
        CHECK_THAT(run.out, ContainsSubstring("\"restraintId\": 5"));
        CHECK_THAT(run.out, ContainsSubstring("face:Solid:side:9991"));
        // And here every LOAD resolves, so the `false` can only be the
        // restraint's.
        CHECK_THAT(run.out, ContainsSubstring("\"faceResolves\": false"));
        const std::string loads = jsonSection(run.out, "loads");
        CHECK(loads.find("false") == std::string::npos);

        const CliRun prose = runCliCommand({"structural-settings", cliPath(file)});
        REQUIRE(prose.exitCode == cli::ExitCode::Success);
        CHECK_THAT(prose.out, ContainsSubstring("UNRESOLVED"));
    }
}

// ---------------------------------------------------------------------------
// Mutation: CLI against the core command
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCli_EveryMutationMatchesTheCoreCommand", "[structural][cli][equivalence]") {
    // THE MANDATORY EQUIVALENCE (the brief's §128-131). Two documents start
    // identical. One is edited through the CLI, the other through the P17-CMD
    // command the CLI is supposed to be calling. Both are reloaded from disk
    // and their canonical fingerprints compared.
    //
    // A CLI that built its own load record, allocated its own id, or skipped
    // the command would differ here even if its own output looked right.
    TempDir temp;

    SECTION("add a load") {
        Part viaCli;
        Part viaCore;
        const std::filesystem::path cliFile = viaCli.save(temp, "add-load-cli");
        const CliRun run = runCliCommand({"structural-load-add", cliPath(cliFile), "4",
                                          "--pressure", "750000", "--face",
                                          Part::text("side:" +
                                                     std::to_string(viaCli.lines[1].value()))});
        INFO(run.err);
        REQUIRE(run.exitCode == cli::ExitCode::Success);

        structural::AddStructuralLoadCommand command{
            viaCore.analysis,
            StructuralLoad{LoadId::fromValue(4U),
                           structural::PressureLoad{.face = viaCore.side(1),
                                                    .magnitude = Pressure::fromSi(750000.0)}}};
        REQUIRE(command.execute(viaCore.document).has_value());
        const std::filesystem::path coreFile = viaCore.save(temp, "add-load-core");

        CHECK(fingerprintOf(cliFile) == fingerprintOf(coreFile));
    }

    SECTION("remove a load") {
        Part viaCli;
        Part viaCore;
        const std::filesystem::path cliFile = viaCli.save(temp, "rm-load-cli");
        const CliRun run =
            runCliCommand({"structural-load-remove", cliPath(cliFile), "1"});
        INFO(run.err);
        REQUIRE(run.exitCode == cli::ExitCode::Success);

        structural::RemoveStructuralLoadCommand command{viaCore.analysis, LoadId::fromValue(1U)};
        REQUIRE(command.execute(viaCore.document).has_value());
        const std::filesystem::path coreFile = viaCore.save(temp, "rm-load-core");

        CHECK(fingerprintOf(cliFile) == fingerprintOf(coreFile));
        // NOT VACUOUS: the load really is gone from both.
        CHECK_THAT(fingerprintOf(cliFile), !ContainsSubstring("load[1"));
    }

    SECTION("add a restraint") {
        Part viaCli;
        Part viaCore;
        const std::filesystem::path cliFile = viaCli.save(temp, "add-res-cli");
        const CliRun run = runCliCommand(
            {"structural-restraint-add", cliPath(cliFile), "3",
             Part::text("side:" + std::to_string(viaCli.lines[2].value())), "ux,uz"});
        INFO(run.err);
        REQUIRE(run.exitCode == cli::ExitCode::Success);

        structural::AddStructuralRestraintCommand command{
            viaCore.analysis,
            StructuralRestraint{RestraintId::fromValue(3U), viaCore.side(2),
                                RestraintComponents::along(DofComponent::Ux)
                                    .add(DofComponent::Uz)}};
        REQUIRE(command.execute(viaCore.document).has_value());
        const std::filesystem::path coreFile = viaCore.save(temp, "add-res-core");

        CHECK(fingerprintOf(cliFile) == fingerprintOf(coreFile));
    }

    SECTION("a component mask written in the other order is the same mask") {
        // `ux,uz` and `uz,ux` must give one value, because the mask is a
        // bitmask with no insertion order to lose. The CLI parser is shown not
        // to have invented one.
        Part first;
        Part second;
        const std::filesystem::path a = first.save(temp, "mask-a");
        const std::filesystem::path b = second.save(temp, "mask-b");
        const std::string face = Part::text("side:" + std::to_string(first.lines[2].value()));
        REQUIRE(runCliCommand({"structural-restraint-add", cliPath(a), "3", face, "ux,uz"})
                    .exitCode == cli::ExitCode::Success);
        REQUIRE(runCliCommand({"structural-restraint-add", cliPath(b), "3", face, "uz,ux"})
                    .exitCode == cli::ExitCode::Success);
        CHECK(fingerprintOf(a) == fingerprintOf(b));
        CHECK_THAT(fingerprintOf(a), ContainsSubstring("ux+uz"));
    }

    SECTION("remove a restraint") {
        Part viaCli;
        Part viaCore;
        const std::filesystem::path cliFile = viaCli.save(temp, "rm-res-cli");
        REQUIRE(runCliCommand({"structural-restraint-remove", cliPath(cliFile), "1"}).exitCode ==
                cli::ExitCode::Success);

        structural::RemoveStructuralRestraintCommand command{viaCore.analysis,
                                                             RestraintId::fromValue(1U)};
        REQUIRE(command.execute(viaCore.document).has_value());
        const std::filesystem::path coreFile = viaCore.save(temp, "rm-res-core");

        CHECK(fingerprintOf(cliFile) == fingerprintOf(coreFile));
    }
}

TEST_CASE("StructuralCli_AFailedMutationLeavesTheFileExactlyAsItWas",
          "[structural][cli][atomic]") {
    // The brief's §15, §54 and §104. A rejected edit must not write, and
    // `runEdit` gives that structurally -- it saves only if the edit succeeded
    // -- so what is checked here is the BYTES, not just the exit code.
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "atomic");
    const std::string before = readText(file);

    struct Case {
        std::string_view what;
        std::vector<std::string> args;
        cli::ExitCode expected;
    };
    const std::string face = Part::text("end_cap");
    const std::vector<Case> cases{
        {"a duplicate load id",
         {"structural-load-add", cliPath(file), "1", "--pressure", "1000", "--face", face},
         cli::ExitCode::Failure},
        {"a load id that is not there",
         {"structural-load-remove", cliPath(file), "99"},
         cli::ExitCode::Failure},
        {"a restraint id that is not there",
         {"structural-restraint-remove", cliPath(file), "99"},
         cli::ExitCode::Failure},
        {"a duplicate restraint id",
         {"structural-restraint-add", cliPath(file), "1", face, "fixed"},
         cli::ExitCode::Failure},
        {"an unknown component",
         {"structural-restraint-add", cliPath(file), "7", face, "rx"},
         cli::ExitCode::UsageError},
        // EXIT 2, NOT 1, and the reason is worth recording: an empty
        // component string is a malformed ARGUMENT, so the parser refuses it
        // before any restraint exists. The core's own empty-mask rule --
        // "a restraint that holds nothing at zero is a record whose intent
        // cannot be acted on" -- is therefore UNREACHABLE through the CLI,
        // because no spelling this parser accepts yields an empty mask. The
        // rule still guards the API; see COMMANDS.md.
        {"an empty component mask",
         {"structural-restraint-add", cliPath(file), "7", face, ""},
         cli::ExitCode::UsageError},
        {"a value that is not a number",
         {"structural-load-add", cliPath(file), "7", "--pressure", "abc", "--face", face},
         cli::ExitCode::UsageError},
        {"a non-finite value",
         {"structural-load-add", cliPath(file), "7", "--pressure", "nan", "--face", face},
         cli::ExitCode::UsageError},
        {"two load kinds at once",
         {"structural-load-add", cliPath(file), "7", "--pressure", "1000", "--gravity",
          "0,0,-9.8", "--face", face},
         cli::ExitCode::UsageError},
        {"no load kind at all",
         {"structural-load-add", cliPath(file), "7", "--face", face},
         cli::ExitCode::UsageError},
        {"a gravity load given a face",
         {"structural-load-add", cliPath(file), "7", "--gravity", "0,0,-9.8", "--face", face},
         cli::ExitCode::UsageError},
        {"a traction with two components",
         {"structural-load-add", cliPath(file), "7", "--traction", "1,2", "--face", face},
         cli::ExitCode::UsageError},
        // A ROLE THE BUILD DOES NOT KNOW IS A COMMAND-LINE FAULT, so exit 2:
        // `fromParse` maps InvalidArgument to "the CLI could not read what it
        // was given", and `coded` deliberately drops the machine code for a
        // usage error "because a usage error prints the usage and exits 2, and
        // a machine code on it would suggest a document state that was never
        // reached". A face that PARSES but names nothing is a different thing
        // and is tested separately -- it is accepted and carried as
        // unresolved intent.
        {"a face role this build does not know",
         {"structural-load-add", cliPath(file), "7", "--pressure", "1000", "--face",
          "face:Solid:not_a_role"},
         cli::ExitCode::UsageError},
        {"a feature that is not in the document",
         {"structural-load-add", cliPath(file), "7", "--pressure", "1000", "--face",
          "face:NoSuchFeature:end_cap"},
         cli::ExitCode::Failure},
        {"a load id that is not a number",
         {"structural-load-remove", cliPath(file), "seven"},
         cli::ExitCode::UsageError},
        {"a second analysis",
         {"structural-analysis-add", cliPath(file)},
         cli::ExitCode::Failure},
    };

    for (const Case& item : cases) {
        INFO(item.what);
        const CliRun run = runCliCommand(item.args);
        INFO(run.err);
        CHECK(run.exitCode == item.expected);
        // THE FILE IS BYTE-FOR-BYTE WHAT IT WAS.
        CHECK(readText(file) == before);
        // And a failed edit says nothing on stdout, which is the stream
        // discipline P15-CLI-001 established.
        CHECK(run.out.empty());
        CHECK_FALSE(run.err.empty());
    }
}

// ---------------------------------------------------------------------------
// Results: CLI against the core
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCli_SolveMatchesTheCore", "[structural][cli][equivalence]") {
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "solve");

    const Result<structural::StructuralSolveOutcome> core = part.solveInCore();
    INFO((core ? std::string{} : core.error().message));
    REQUIRE(core.has_value());

    const CliRun run = runCliCommand({"structural-solve", cliPath(file), "--json"});
    INFO(run.err);
    REQUIRE(run.exitCode == cli::ExitCode::Success);

    // EVERY SCALAR THE COMMAND PRINTS, against the core's own accessor. These
    // are equalities and not tolerances: the CLI reloaded the same file,
    // regenerated the same geometry and meshed it with the same deterministic
    // mesher, so any difference at all would mean the CLI did something else.
    CHECK(jsonNumber(run.out, "freeEquations") == static_cast<double>(core->freeEquations));
    CHECK(jsonNumber(run.out, "normalizedResidual") == core->residual.normalized);
    CHECK(jsonNumber(run.out, "pivotRatio") == core->pivotRatio);
    CHECK_THAT(jsonQuantity(run.out, "strainEnergy"),
               WithinRel(core->strainEnergy.si(), 1e-12));
    CHECK_THAT(jsonQuantity(run.out, "largestDisplacement"),
               WithinRel(core->largestDisplacement.si(), 1e-12));
    CHECK_THAT(jsonQuantity(run.out, "largestVonMises"),
               WithinRel(core->largestVonMises.si(), 1e-12));
    // Not vacuous: the solve produced real numbers.
    CHECK(core->largestVonMises.si() > 0.0);
    CHECK(core->freeEquations > 0U);

    // No array is dumped: a summary at any size (the brief's §24).
    CHECK(run.out.size() < 2048U);
}

TEST_CASE("StructuralCli_DisplacementSummaryMatchesTheCore",
          "[structural][cli][equivalence]") {
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "displacement");
    const Result<structural::StructuralSolveOutcome> core = part.solveInCore();
    REQUIRE(core.has_value());

    const CliRun run = runCliCommand({"structural-displacement", cliPath(file), "--json"});
    INFO(run.err);
    REQUIRE(run.exitCode == cli::ExitCode::Success);

    const std::span<const structural::NodalDisplacement> displacements =
        core->fields.displacements();
    REQUIRE_FALSE(displacements.empty());
    // The core's own largest, found the same way the CLI must: by magnitude,
    // tie-broken by ascending NodeId.
    const structural::NodalDisplacement* worst = nullptr;
    for (const structural::NodalDisplacement& d : displacements) {
        if (worst == nullptr || d.magnitude.si() > worst->magnitude.si() ||
            (d.magnitude.si() == worst->magnitude.si() &&
             d.node.value() < worst->node.value())) {
            worst = &d;
        }
    }
    REQUIRE(worst != nullptr);

    CHECK(jsonNumber(run.out, "nodeCount") == static_cast<double>(displacements.size()));
    CHECK(jsonNumber(run.out, "node") == static_cast<double>(worst->node.value()));
    CHECK_THAT(jsonNumber(run.out, "max"), WithinRel(worst->magnitude.si(), 1e-12));
    CHECK(jsonText(run.out, "unit") == "m");
    CHECK(worst->magnitude.si() > 0.0);

    // THE VALUE INSIDE `largest`, which the id check above does not reach --
    // the same gap probe L5 exposed on the stress side.
    const std::string largest = jsonSection(run.out, "largest");
    CHECK_THAT(jsonQuantity(largest, "magnitude"), WithinRel(worst->magnitude.si(), 1e-12));
    const std::string displacement = jsonSection(largest, "displacement");
    CHECK_THAT(jsonNumber(displacement, "x"), WithinRel(worst->displacement.x.si(), 1e-12));
    CHECK_THAT(jsonNumber(displacement, "y"), WithinRel(worst->displacement.y.si(), 1e-12));
    CHECK_THAT(jsonNumber(displacement, "z"), WithinRel(worst->displacement.z.si(), 1e-12));

    // Each component's range, against the core's own span.
    const auto coreRange = [&displacements](auto project) {
        double low = project(displacements.front());
        double high = low;
        for (const structural::NodalDisplacement& d : displacements) {
            const double v = project(d);
            low = v < low ? v : low;
            high = v > high ? v : high;
        }
        return std::pair{low, high};
    };
    const auto checkComponent = [&](std::string_view key, auto project) {
        INFO(key);
        const std::string section = jsonSection(run.out, key);
        const auto [low, high] = coreRange(project);
        CHECK_THAT(jsonNumber(section, "min"), WithinRel(low, 1e-12));
        CHECK_THAT(jsonNumber(section, "max"), WithinRel(high, 1e-12));
    };
    checkComponent("ux",
                   [](const structural::NodalDisplacement& d) { return d.displacement.x.si(); });
    checkComponent("uy",
                   [](const structural::NodalDisplacement& d) { return d.displacement.y.si(); });
    checkComponent("uz",
                   [](const structural::NodalDisplacement& d) { return d.displacement.z.si(); });

    // THE TIE-BREAK RULE, encoded: the reported node is the LOWEST id among
    // those carrying the maximum. How many carry it is MEASURED, because a
    // rule no fixture can exercise is a rule no test proves.
    std::size_t atMaximum = 0;
    for (const structural::NodalDisplacement& d : displacements) {
        if (d.magnitude.si() == worst->magnitude.si()) {
            ++atMaximum;
            CHECK(d.node.value() >= worst->node.value());
        }
    }
    WARN(std::format("{} of {} nodes carry the maximum displacement magnitude", atMaximum,
                     displacements.size()));
}

TEST_CASE("StructuralCli_StressSummaryMatchesTheCore", "[structural][cli][equivalence]") {
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "stress");
    const Result<structural::StructuralSolveOutcome> core = part.solveInCore();
    REQUIRE(core.has_value());

    const CliRun run = runCliCommand({"structural-stress", cliPath(file), "--json"});
    INFO(run.err);
    REQUIRE(run.exitCode == cli::ExitCode::Success);

    const std::span<const structural::ElementFields> elements = core->fields.elements();
    REQUIRE_FALSE(elements.empty());
    const structural::ElementFields* worst = nullptr;
    for (const structural::ElementFields& e : elements) {
        if (worst == nullptr || e.vonMises.si() > worst->vonMises.si() ||
            (e.vonMises.si() == worst->vonMises.si() &&
             e.element.value() < worst->element.value())) {
            worst = &e;
        }
    }
    REQUIRE(worst != nullptr);

    // THE TETRAHEDRON COUNT, not the mesh total. `Mesh::elementCount()` is
    // triangles PLUS tetrahedra, so a 12-tetrahedron block reports 24 -- and
    // reporting that beside a per-tetrahedron stress range was a real defect
    // this comparison found. Both counts are now printed, named apart.
    CHECK(jsonNumber(run.out, "tetrahedronCount") == static_cast<double>(elements.size()));
    CHECK(jsonNumber(run.out, "elementCount") >= static_cast<double>(elements.size()));
    CHECK(jsonNumber(run.out, "element") == static_cast<double>(worst->element.value()));
    CHECK(jsonText(run.out, "unit") == "Pa");

    // THE VALUES, NOT ONLY THE IDS. Probe L5 divided the largest von Mises by
    // 1e6 while still labelling it Pa, and SURVIVED: the id was compared, the
    // range was compared, and the number inside `largest` was read by nothing.
    // Both are now compared, scoped so the repeated `vonMises` key cannot be
    // confused, and required to agree with each other.
    const std::string largest = jsonSection(run.out, "largest");
    CHECK_THAT(jsonQuantity(largest, "vonMises"), WithinRel(worst->vonMises.si(), 1e-12));
    CHECK_THAT(jsonQuantity(largest, "sigma1"),
               WithinRel(worst->principalStress.sigma1.si(), 1e-12));
    const std::string range = jsonSection(run.out, "vonMises");
    CHECK_THAT(jsonNumber(range, "max"), WithinRel(worst->vonMises.si(), 1e-12));
    // The element carrying the maximum must carry the range's maximum, which
    // is the relation a corrupted value in either place would break.
    CHECK_THAT(jsonNumber(range, "max"), WithinRel(jsonQuantity(largest, "vonMises"), 1e-15));

    // Every stress component's range, against the core's own span. A component
    // read from the wrong member would otherwise be invisible.
    const std::string components = jsonSection(run.out, "components");
    const auto coreRange = [&elements](auto project) {
        double low = project(elements.front());
        double high = low;
        for (const structural::ElementFields& e : elements) {
            const double v = project(e);
            low = v < low ? v : low;
            high = v > high ? v : high;
        }
        return std::pair{low, high};
    };
    const auto checkComponent = [&](std::string_view key, auto project) {
        INFO(key);
        const std::string section = jsonSection(components, key);
        const auto [low, high] = coreRange(project);
        CHECK_THAT(jsonNumber(section, "min"), WithinRel(low, 1e-12));
        CHECK_THAT(jsonNumber(section, "max"), WithinRel(high, 1e-12));
    };
    checkComponent("xx", [](const structural::ElementFields& e) { return e.stress.xx.si(); });
    checkComponent("yy", [](const structural::ElementFields& e) { return e.stress.yy.si(); });
    checkComponent("zz", [](const structural::ElementFields& e) { return e.stress.zz.si(); });
    checkComponent("xy", [](const structural::ElementFields& e) { return e.stress.xy.si(); });
    checkComponent("yz", [](const structural::ElementFields& e) { return e.stress.yz.si(); });
    checkComponent("zx", [](const structural::ElementFields& e) { return e.stress.zx.si(); });

    // THE TIE-BREAK RULE, encoded. The reported element must be the LOWEST id
    // among those carrying the maximum -- so a reversed rule fails whenever a
    // tie exists. Whether this fixture HAS one is measured below rather than
    // assumed, because an unreachable rule is not a tested rule.
    std::size_t atMaximum = 0;
    for (const structural::ElementFields& e : elements) {
        if (e.vonMises.si() == worst->vonMises.si()) {
            ++atMaximum;
            CHECK(e.element.value() >= worst->element.value());
        }
    }
    WARN(std::format("{} of {} elements carry the maximum von Mises", atMaximum,
                     elements.size()));
    // ELEMENT, NOT NODAL, and the payload says which (the brief's §41). A CLI
    // that averaged a Tet4's constant state onto nodes would have invented a
    // smoothing policy the core does not have.
    CHECK(jsonText(run.out, "domain") == "element");
    CHECK(worst->vonMises.si() > 0.0);
}

TEST_CASE("StructuralCli_ReactionSummaryMatchesTheCore", "[structural][cli][equivalence]") {
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "reactions");
    const Result<structural::StructuralSolveOutcome> core = part.solveInCore();
    REQUIRE(core.has_value());

    const CliRun run = runCliCommand({"structural-reactions", cliPath(file), "--json"});
    INFO(run.err);
    REQUIRE(run.exitCode == cli::ExitCode::Success);

    const structural::SupportReactions& reactions = core->reactions;
    CHECK(jsonNumber(run.out, "constrainedNodes") ==
          static_cast<double>(core->result.reactions().size()));
    CHECK_THAT(jsonNumber(run.out, "z"), WithinRel(reactions.totalForce().z.si(), 1e-12));
    CHECK(jsonNumber(run.out, "restraintId") ==
          static_cast<double>(reactions.restraints().front().restraint.value()));

    // THE CORE'S SIGN, UNTOUCHED (the brief's §43). The applied pressure acts
    // inward on the end cap, so the support reaction opposes it -- and the
    // test states the relation rather than a literal, so a sign flip anywhere
    // in the chain fails here.
    CHECK(reactions.totalForce().z.si() != 0.0);
    CHECK_THAT(jsonNumber(run.out, "z") + core->forceBalance.external.z.si(),
               Catch::Matchers::WithinAbs(0.0,
                                          1e-6 * std::abs(reactions.totalForce().z.si())));
}

TEST_CASE("StructuralCli_ValidationMatchesTheCore", "[structural][cli][equivalence]") {
    TempDir temp;

    SECTION("an acceptable analysis") {
        Part part;
        const std::filesystem::path file = part.save(temp, "valid-ok");
        REQUIRE(part.mesher.generate(part.document, part.regenerator, part.control).has_value());
        const structural::StructuralValidationReport core =
            structural::validateStructuralAnalysisForSolve(part.document, part.regenerator,
                                                           part.mesher, part.analysis);
        REQUIRE(core.acceptable());

        const CliRun run = runCliCommand({"structural-validate", cliPath(file), "--json"});
        INFO(run.err);
        CHECK(run.exitCode == cli::ExitCode::Success);
        CHECK(jsonText(run.out, "status") == std::string{toString(core.status())});
        CHECK(jsonText(run.out, "reachedStage") == std::string{toString(core.reachedStage)});
        CHECK_THAT(run.out, ContainsSubstring("\"acceptable\": true"));
    }

    SECTION("an unresolved restraint target") {
        Part part;
        StructuralAnalysisDefinition definition = part.definition();
        definition.restraints.push_back(StructuralRestraint{
            RestraintId::fromValue(5U), part.unresolvable(), RestraintComponents::fixed()});
        part.setAnalysis(definition);
        const std::filesystem::path file = part.save(temp, "valid-unresolved");
        REQUIRE(part.mesher.generate(part.document, part.regenerator, part.control).has_value());
        const structural::StructuralValidationReport core =
            structural::validateStructuralAnalysisForSolve(part.document, part.regenerator,
                                                           part.mesher, part.analysis);
        REQUIRE_FALSE(core.acceptable());
        REQUIRE_FALSE(core.findings.empty());

        const CliRun run = runCliCommand({"structural-validate", cliPath(file), "--json"});
        // THE REPORT IS THE ANSWER AND THE GATE REACHES THE EXIT STATUS: the
        // payload is on stdout AND the process fails, which is the brief's
        // §120 and `mesh-validate`'s own precedent.
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK_FALSE(run.out.empty());
        CHECK(jsonText(run.out, "status") == std::string{toString(core.status())});
        // THE SAME CODE, in the core's own staged order (the brief's §142).
        CHECK(jsonText(run.out, "code") == std::string{toString(core.findings.front().code)});
        CHECK_THAT(run.out, ContainsSubstring("\"acceptable\": false"));
    }
}

// ---------------------------------------------------------------------------
// The failure matrix
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCli_EveryRefusalClassReachesTheExitStatus",
          "[structural][cli][failure]") {
    // The brief's §29-35 and §51. Each of these is a real document state, and
    // each must fail through the CORE's gate with the core's own diagnostic
    // code -- never a CLI reinterpretation, and never exit 0.
    TempDir temp;

    SECTION("a missing material") {
        Part part{/*withMaterial=*/false};
        const std::filesystem::path file = part.save(temp, "no-material");
        for (const std::string_view command :
             {"structural-solve", "structural-displacement", "structural-stress",
              "structural-reactions"}) {
            INFO(command);
            const CliRun run = runCliCommand({std::string{command}, cliPath(file), "--json"});
            CHECK(run.exitCode == cli::ExitCode::Failure);
            // "Could not answer": nothing on stdout, the code on stderr.
            CHECK(run.out.empty());
            CHECK_THAT(run.err, ContainsSubstring("material"));
        }
        // NO CLI DEFAULT WAS SUBSTITUTED (the brief's §155): there is no
        // --default-E, and the refusal is the material stage's.
        const CliRun validate = runCliCommand({"structural-validate", cliPath(file), "--json"});
        CHECK(validate.exitCode == cli::ExitCode::Failure);
        CHECK_THAT(validate.out, ContainsSubstring("material"));
    }

    SECTION("an under-constrained analysis") {
        Part part{/*withMaterial=*/true, /*withRestraint=*/false};
        const std::filesystem::path file = part.save(temp, "under-constrained");
        const CliRun run = runCliCommand({"structural-solve", cliPath(file), "--json"});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("insufficient_restraint"));
        // NOT AUTO-PINNED (the brief's §153). The CLI has no flag that would
        // add a support, and the diagnostic is the core's own.
        CHECK_THAT(run.err, ContainsSubstring("six rigid-body modes"));
    }

    SECTION("an unresolved load target") {
        Part part;
        StructuralAnalysisDefinition definition = part.definition();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(8U),
            structural::PressureLoad{.face = part.unresolvable(),
                                     .magnitude = Pressure::fromSi(1.0e5)}});
        part.setAnalysis(definition);
        const std::filesystem::path file = part.save(temp, "unresolved-load");
        const CliRun run = runCliCommand({"structural-solve", cliPath(file), "--json"});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("loads_unresolvable"));
        // THE OFFENDING LOAD WAS NOT DROPPED (the brief's §31): inspection
        // still reports it.
        const CliRun settings = runCliCommand({"structural-settings", cliPath(file), "--json"});
        CHECK(settings.exitCode == cli::ExitCode::Success);
        CHECK_THAT(settings.out, ContainsSubstring("\"loadId\": 8"));
    }

    SECTION("an unresolved restraint target") {
        Part part;
        StructuralAnalysisDefinition definition = part.definition();
        definition.restraints.push_back(StructuralRestraint{
            RestraintId::fromValue(5U), part.unresolvable(), RestraintComponents::fixed()});
        part.setAnalysis(definition);
        const std::filesystem::path file = part.save(temp, "unresolved-restraint");
        const CliRun run = runCliCommand({"structural-solve", cliPath(file), "--json"});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("restraints_unresolvable"));
    }

    SECTION("a document with no structural analysis") {
        Document bare{"Bare"};
        const std::filesystem::path file = temp.path() / "bare.bcad";
        REQUIRE(io::saveDocument(bare, file).has_value());
        for (const std::string_view command :
             {"structural-settings", "structural-validate", "structural-solve",
              "structural-displacement", "structural-stress", "structural-reactions"}) {
            INFO(command);
            const CliRun run = runCliCommand({std::string{command}, cliPath(file), "--json"});
            CHECK(run.exitCode == cli::ExitCode::Failure);
            CHECK(run.out.empty());
            CHECK_THAT(run.err, ContainsSubstring("no_structural_analysis"));
        }
    }

    SECTION("a document that does not exist") {
        const std::filesystem::path missing = temp.path() / "nothing.bcad";
        const CliRun run = runCliCommand({"structural-solve", cliPath(missing), "--json"});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("document_not_loaded"));
    }

    SECTION("a usage error") {
        Part part;
        const std::filesystem::path file = part.save(temp, "usage");
        for (const std::vector<std::string>& args :
             {std::vector<std::string>{"structural-solve"},
              {"structural-solve", cliPath(file), "--nonsense"},
              {"structural-solve", cliPath(file), cliPath(file)}}) {
            const CliRun run = runCliCommand(args);
            CHECK(run.exitCode == cli::ExitCode::UsageError);
            CHECK(run.out.empty());
        }
    }
}

TEST_CASE("StructuralCli_AmbiguityIsRefusedRatherThanGuessed", "[structural][cli][failure]") {
    // The brief's §103. Two analyses and no selector is an explicit refusal,
    // never a first match -- which is `theControl`'s rule in P16 and is worth
    // keeping because "whichever came first" is not reproducible.
    TempDir temp;
    Part part;
    StructuralAnalysisDefinition second;
    second.mesh = part.control;
    auto other = StructuralAnalysis::create("Second", second);
    REQUIRE(other.has_value());
    (void)require(part.document.addObject(std::move(*other)));
    const std::filesystem::path file = part.save(temp, "ambiguous");

    for (const std::string_view command :
         {"structural-settings", "structural-validate", "structural-solve",
          "structural-displacement", "structural-stress", "structural-reactions"}) {
        INFO(command);
        const CliRun run = runCliCommand({std::string{command}, cliPath(file), "--json"});
        CHECK(run.exitCode == cli::ExitCode::Failure);
        CHECK(run.out.empty());
        CHECK_THAT(run.err, ContainsSubstring("ambiguous_structural_analysis"));
    }
    // And the edits refuse it too, by the same name.
    const CliRun edit =
        runCliCommand({"structural-load-remove", cliPath(file), "1"});
    CHECK(edit.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(edit.err, ContainsSubstring("ambiguous_structural_analysis"));
}

// ---------------------------------------------------------------------------
// Output discipline and persistence
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCli_StructuredOutputIsTheWholeOfStdout",
          "[structural][cli][structured]") {
    // The brief's §74 and §78. Stdout in machine mode is exactly one payload:
    // it begins with the opening brace, ends with the closing one and a single
    // newline, and has nothing before or after. A backend warning, a progress
    // line or a debug banner anywhere would break one of these.
    //
    // This runs the MESHER and the SOLVER, so Netgen's sizing chatter -- the
    // defect P16-CLI-001 found, where "WARNING: RestrictLocalH" landed on
    // stdout ahead of the brace -- is live during the capture.
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "stdout");

    for (const std::string_view command :
         {"structural-settings", "structural-validate", "structural-solve",
          "structural-displacement", "structural-stress", "structural-reactions"}) {
        INFO(command);
        const CliRun run = runCliCommand({std::string{command}, cliPath(file), "--json"});
        INFO(run.err);
        REQUIRE(run.exitCode == cli::ExitCode::Success);
        REQUIRE_FALSE(run.out.empty());
        CHECK(run.out.front() == '{');
        CHECK(run.out.back() == '\n');
        CHECK(run.out[run.out.size() - 2U] == '}');
        // Balanced, and every brace accounted for -- a cheap structural check
        // that a prefix or suffix would break. The whole-payload parse gate is
        // the workflow script's, with a real JSON parser.
        std::size_t depth = 0;
        std::size_t closes = 0;
        bool inString = false;
        for (std::size_t i = 0; i < run.out.size(); ++i) {
            const char c = run.out[i];
            if (c == '"' && (i == 0 || run.out[i - 1U] != '\\')) {
                inString = !inString;
            }
            if (inString) {
                continue;
            }
            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                REQUIRE(depth > 0U);
                --depth;
                ++closes;
            }
        }
        CHECK(depth == 0U);
        CHECK(closes > 0U);
    }
}

TEST_CASE("StructuralCli_SolvingNeverChangesTheDocument",
          "[structural][cli][persistence]") {
    // ADR-048's rule, reached through the CLI (the brief's §25, §60 and §190).
    // Four result commands run against one file, each solving, and the bytes
    // must not move: no residual, no max stress and no "last solved" field can
    // appear, because a report never writes.
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "unchanged");
    const std::string before = readText(file);

    for (const std::string_view command :
         {"structural-solve", "structural-displacement", "structural-stress",
          "structural-reactions", "structural-validate"}) {
        INFO(command);
        const CliRun run = runCliCommand({std::string{command}, cliPath(file), "--json"});
        INFO(run.err);
        REQUIRE(run.exitCode == cli::ExitCode::Success);
        CHECK(readText(file) == before);
    }

    // NOT VACUOUS: a solve really happened, and really produced a result.
    const CliRun solve = runCliCommand({"structural-solve", cliPath(file), "--json"});
    CHECK(jsonQuantity(solve.out, "largestVonMises") > 0.0);

    // And the file still carries no result, by name.
    for (const std::string_view forbidden :
         {"displacement", "stress", "vonMises", "reaction", "residual", "node", "element"}) {
        INFO(forbidden);
        CHECK(readText(file).find(std::format("\"{}", forbidden)) == std::string::npos);
    }
}

TEST_CASE("StructuralCli_ARepeatedCommandGivesTheSameBytes",
          "[structural][cli][determinism]") {
    // The brief's §141. The structured writer is insertion-ordered, the mesher
    // is deterministic and the solver is direct, so the whole payload is
    // byte-stable -- which is the strong form, and is asserted as such rather
    // than as semantic equality.
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "determinism");

    for (const std::string_view command :
         {"structural-settings", "structural-solve", "structural-displacement",
          "structural-stress", "structural-reactions", "structural-validate"}) {
        INFO(command);
        const CliRun first = runCliCommand({std::string{command}, cliPath(file), "--json"});
        REQUIRE(first.exitCode == cli::ExitCode::Success);
        for (int i = 0; i < 3; ++i) {
            const CliRun again = runCliCommand({std::string{command}, cliPath(file), "--json"});
            CHECK(again.out == first.out);
            CHECK(again.exitCode == first.exitCode);
        }
    }
}

TEST_CASE("StructuralCli_TheProseAndTheJsonCarryTheSameNumbers",
          "[structural][cli][structured]") {
    // The brief's §7 and §80. Human output may be concise, but it must derive
    // from the same canonical data -- so the two forms are compared rather
    // than assumed to agree, and the prose is not used for any numerical
    // qualification elsewhere.
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "prose");

    const CliRun json = runCliCommand({"structural-solve", cliPath(file), "--json"});
    const CliRun prose = runCliCommand({"structural-solve", cliPath(file)});
    REQUIRE(json.exitCode == cli::ExitCode::Success);
    REQUIRE(prose.exitCode == cli::ExitCode::Success);
    CHECK(prose.out.front() != '{');

    const double nodes = jsonNumber(json.out, "nodeCount");
    const double tetrahedra = jsonNumber(json.out, "tetrahedronCount");
    const double free = jsonNumber(json.out, "freeEquations");
    CHECK_THAT(prose.out, ContainsSubstring(std::format("{} nodes", static_cast<long>(nodes))));
    // TETRAHEDRA IN BOTH FORMS. The prose said "elements" until the
    // core-equivalence test showed that count was the mesh total -- triangles
    // included -- beside per-tetrahedron physics.
    CHECK_THAT(prose.out,
               ContainsSubstring(std::format("{} tetrahedra", static_cast<long>(tetrahedra))));
    CHECK_THAT(prose.out,
               ContainsSubstring(std::format("{} free equations", static_cast<long>(free))));
    // And the two counts really do differ on this fixture, so the distinction
    // is not vacuous.
    CHECK(jsonNumber(json.out, "elementCount") > tetrahedra);
}

// ---------------------------------------------------------------------------
// The boundary the CLI must not cross
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCli_TheCliAcceptsNoMeshLocalTarget", "[structural][cli][authority]") {
    // The brief's §11, §17 and §156. A load or restraint target is a CAD face
    // and nothing else. There is no option for a node, an element, a facet or
    // a coordinate -- so this checks that each spelling someone might try is
    // REFUSED, and that the refusal is the argument parser's rather than a
    // silent reinterpretation.
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "authority");
    const std::string before = readText(file);

    for (const std::string_view target :
         {"node:1", "element:1", "facet:1", "triangle:9", "1", "0.02,0.015,0.02",
          "nearest:0,0,0"}) {
        INFO(target);
        const CliRun run = runCliCommand({"structural-load-add", cliPath(file), "7",
                                          "--pressure", "1000", "--face", std::string{target}});
        CHECK(run.exitCode != cli::ExitCode::Success);
        CHECK(readText(file) == before);
    }
    // And no option by those names exists at all: an unknown option is a
    // usage error, which is what makes the absence provable rather than
    // merely unobserved.
    for (const std::string_view option : {"--node", "--element", "--facet", "--at"}) {
        INFO(option);
        const CliRun run = runCliCommand({"structural-load-add", cliPath(file), "7",
                                          std::string{option}, "1"});
        CHECK(run.exitCode == cli::ExitCode::UsageError);
        CHECK(readText(file) == before);
    }
}

TEST_CASE("StructuralCli_ThereIsNoFlagThatBypassesValidation",
          "[structural][cli][authority]") {
    // The brief's §151-155. Each of these would make a refused analysis solve,
    // and none exists. An unknown option is a usage error, so their absence is
    // checked rather than asserted in a comment.
    TempDir temp;
    Part part{/*withMaterial=*/true, /*withRestraint=*/false};
    const std::filesystem::path file = part.save(temp, "no-bypass");

    for (const std::string_view flag :
         {"--force", "--ignore-validation", "--ignore-stale", "--auto-pin", "--regularize",
          "--default-E", "--skip-quality"}) {
        INFO(flag);
        const CliRun run =
            runCliCommand({"structural-solve", cliPath(file), std::string{flag}});
        CHECK(run.exitCode == cli::ExitCode::UsageError);
    }
    // And the under-constrained model still fails without them.
    CHECK(runCliCommand({"structural-solve", cliPath(file), "--json"}).exitCode ==
          cli::ExitCode::Failure);
}

TEST_CASE("StructuralCli_EveryStructuralCommandIsInTheHelp", "[structural][cli]") {
    // The brief's §186. A command nobody can discover is a command nobody can
    // use, and `help` is the one listing.
    const CliRun run = runCliCommand({"--help"});
    REQUIRE(run.exitCode == cli::ExitCode::Success);
    for (const std::string_view command :
         {"structural-settings", "structural-validate", "structural-solve",
          "structural-displacement", "structural-stress", "structural-reactions",
          "structural-analysis-add", "structural-load-add", "structural-load-remove",
          "structural-restraint-add", "structural-restraint-remove"}) {
        INFO(command);
        CHECK_THAT(run.out, ContainsSubstring(std::string{command}));
    }
    // And help takes no document, so it cannot have loaded or solved anything
    // (the brief's §187).
    CHECK(run.err.empty());
}

TEST_CASE("StructuralCli_TheEditsAreScriptableThroughBatch", "[structural][cli]") {
    // The edit spine's own claim (ADR-009): registering a verb makes it a
    // batch verb, "in the same one-transaction-or-nothing script as a mate
    // verb, with nothing added to BatchCommand.cpp". This is that claim
    // tested, including the all-or-nothing half.
    TempDir temp;
    Part part;
    const std::filesystem::path file = part.save(temp, "batch");
    const std::string before = readText(file);

    const std::filesystem::path good = temp.path() / "good.txt";
    writeFile(good, std::format("structural-load-add 4 --pressure 500000 --face {}\n"
                                "structural-restraint-add 2 {} uy\n",
                                Part::text("side:" + std::to_string(part.lines[1].value())),
                                Part::text("side:" + std::to_string(part.lines[2].value()))));
    const CliRun applied = runCliCommand({"batch", cliPath(file), cliPath(good)});
    INFO(applied.err);
    CHECK(applied.exitCode == cli::ExitCode::Success);
    CHECK(readText(file) != before);
    CHECK_THAT(fingerprintOf(file), ContainsSubstring("load[4"));
    CHECK_THAT(fingerprintOf(file), ContainsSubstring("restraint[2"));

    // ONE TRANSACTION: a script whose second line fails writes nothing at all.
    const std::string afterGood = readText(file);
    const std::filesystem::path bad = temp.path() / "bad.txt";
    writeFile(bad, std::format("structural-load-add 9 --pressure 100 --face {}\n"
                               "structural-load-add 9 --pressure 200 --face {}\n",
                               Part::text("end_cap"), Part::text("end_cap")));
    const CliRun refused = runCliCommand({"batch", cliPath(file), cliPath(bad)});
    CHECK(refused.exitCode == cli::ExitCode::Failure);
    CHECK(readText(file) == afterGood);
    CHECK_THAT(fingerprintOf(file), !ContainsSubstring("load[9"));
}
