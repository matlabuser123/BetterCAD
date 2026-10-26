// Persistence and determinism of the thermal reference models (P18-REFMOD-001: "save/load/re-solve", "determinism").
//
// THE CONTRACT BEING CHECKED. A saved file is the CANONICAL INTENT and nothing derived: no mesh, no temperature, no flux, no
// balance. Solving does not change the file (byte for byte), and a document loaded from the file, regenerated, meshed and
// solved again gives the SAME answer as the document that was saved -- compared bit for bit, because nothing in the chain
// is allowed to depend on a clock, an address, a hash order or a thread.

#include "reference/ThermalReferenceSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/io/DocumentFile.hpp>

#include <bit>
#include <functional>

namespace {

using namespace bettercad;
using namespace bettercad::test::thermalref;

struct Fingerprint {
    std::uint64_t hash = 14695981039346656037ULL;
    std::size_t values = 0;

    void add(double value) {
        hash ^= std::bit_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
        ++values;
    }
    friend bool operator==(const Fingerprint&, const Fingerprint&) = default;
};

/// Every nodal temperature of every stored state, the final gradients and fluxes, and the balance residual -- all of what the run
/// computed, as one hash of the bit patterns.
[[nodiscard]] Fingerprint fingerprintOf(const thermal::ThermalAnalysisRun& run) {
    Fingerprint f;
    REQUIRE(run.post.has_value());
    REQUIRE(run.balance.has_value());
    for (const thermal::ThermalPostState& state : run.post->states()) {
        for (const Temperature& t : state.temperatures) {
            f.add(t.si());
        }
    }
    for (const thermal::TemperatureGradient3& g : run.post->finalState().fields.gradients) {
        f.add(g.x.si());
        f.add(g.y.si());
        f.add(g.z.si());
    }
    for (const thermal::HeatFlux3& q : run.post->finalState().fields.heatFluxes) {
        f.add(q.x.si());
        f.add(q.y.si());
        f.add(q.z.si());
    }
    for (const thermal::ThermalPostState& state : run.post->states()) {
        f.add(state.time.has_value() ? state.time->si() : -1.0);
    }
    for (const thermal::ThermalHeatBalance& step : run.balance->states()) {
        f.add(step.terms.appliedSurfaceHeat.si());
        f.add(step.terms.generatedHeat.si());
        f.add(step.terms.convectionHeat.si());
        f.add(step.terms.prescribedTemperatureReaction.si());
        f.add(step.terms.storedEnergyRate.has_value() ? step.terms.storedEnergyRate->si() : 0.0);
        f.add(step.stepSize.has_value() ? step.stepSize->si() : 0.0);
        for (const thermal::ReactionHeat& r : step.reactions) {
            f.add(r.heatInto.si());
        }
    }
    f.add(run.balance->worstNormalizedResidual());
    return f;
}

struct Model {
    const char* name;
    std::function<Result<reference::ThermalBlockModel>()> build;
};

[[nodiscard]] std::vector<Model> persistedModels() {
    return {
        Model{"RM-THERM-02 slab", [] { return reference::buildThermalSlabReferenceModel(); }},
        Model{"RM-THERM-05 convection", [] { return reference::buildThermalConvectionReferenceModel(); }},
        Model{"RM-THERM-08 lumped", [] { return reference::buildThermalLumpedReferenceModel(); }},
        Model{"RM-THERM-02 surface flux", [] { return reference::buildThermalSurfaceFluxReferenceModel(); }},
        Model{"RM-THERM-04 generation", [] { return reference::buildThermalGenerationReferenceModel(); }},
        Model{"RM-THERM-08 transient slab", [] { return reference::buildThermalTransientSlabReferenceModel(); }},
    };
}

} // namespace

TEST_CASE("ThermalReference_Persistence_SaveLoadReSolve", "[refmod][thermal][persistence]") {
    for (const Model& model : persistedModels()) {
        INFO(model.name);
        auto built = model.build();
        REQUIRE(built.has_value());
        test::TempDir dir;
        const auto first = dir.path() / "model.bcad";
        const auto second = dir.path() / "after-solve.bcad";

        // THE FILE IS WRITTEN BEFORE ANYTHING IS REGENERATED, MESHED OR SOLVED: the document exactly as the builder made it. (A
        // `Run` regenerates, meshes and solves in its constructor, so saving after it would compare two post-solve files.)
        REQUIRE(io::saveDocument(built->document, first).has_value());
        const std::string bytes = test::readFile(first);
        CHECK_FALSE(bytes.empty());

        Run original(std::move(built->document));
        REQUIRE(original.run().accepted());
        const Fingerprint before = fingerprintOf(original.run());

        // Regenerating, meshing and solving did not edit the document: the file written AFTER is the file written BEFORE, byte for byte,
        // and so is the file after a second solve.
        original.rerun();
        REQUIRE(original.run().accepted());
        REQUIRE(io::saveDocument(original.document(), second).has_value());
        CHECK(bytes == test::readFile(second));
        // NO DERIVED STATE IN THE FILE: not a mesh, not a result, not a balance.
        for (const char* word : {"gradient", "reaction", "tetrahedra", "triangle", "balance", "solution", "nodal", "temperatures", "heat_flux_field", "mesh_nodes", "stored_energy", "residual_history"}) {
            INFO("the saved file mentions '" << word << "'");
            CHECK(bytes.find(word) == std::string::npos);
        }

        // LOAD, REGENERATE, MESH, SOLVE: the same answer, bit for bit.
        auto loaded = io::loadDocument(first);
        REQUIRE(loaded.has_value());
        Run reloaded(std::move(*loaded));
        INFO(describe(reloaded.run().validation));
        REQUIRE(reloaded.run().accepted());
        const Fingerprint after = fingerprintOf(reloaded.run());
        CHECK(after.values == before.values);
        CHECK(after.hash == before.hash);
        CHECK(reloaded.mesh().nodes().size() == original.mesh().nodes().size());
        CHECK(reloaded.mesh().tetrahedra().size() == original.mesh().tetrahedra().size());

        // and the reloaded document, saved again, is the same file: the round trip is a fixed point
        const auto third = dir.path() / "round-trip.bcad";
        REQUIRE(io::saveDocument(reloaded.document(), third).has_value());
        CHECK(test::readFile(third) == bytes);
        evidence(std::string{"persistence "} + model.name + ": " + std::to_string(bytes.size()) + " bytes, " +
                 std::to_string(after.values) + " values hashed, fingerprint equal: " + (after == before ? "yes" : "NO"));
    }
}

TEST_CASE("ThermalReference_Persistence_EditedFileIsAnotherProblem", "[refmod][thermal][persistence]") {
    // The converse: a file is the INTENT, so editing the intent in the file changes the answer. The slab's end temperature is
    // 300 K in the saved text; replacing it by 200 K must give the new closed-form flux k dT / L = 8 x 200 / 1 = 1600 W/m^2.
    auto built = reference::buildThermalSlabReferenceModel();
    REQUIRE(built.has_value());
    test::TempDir dir;
    const auto path = dir.path() / "slab.bcad";
    REQUIRE(io::saveDocument(built->document, path).has_value());
    std::string text = test::readFile(path);
    // the held temperatures are stored in K; find the cold one by its value
    const std::size_t at = text.find("300.0");
    REQUIRE(at != std::string::npos);
    text.replace(at, 5, "200.0");
    test::writeFile(path, text);
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Run edited(std::move(*loaded));
    REQUIRE(edited.run().accepted());
    double qmax = 0.0;
    for (const HeatFlux& m : edited.post().finalState().fields.heatFluxMagnitudes) {
        qmax = std::max(qmax, m.si());
    }
    requireClose(qmax, 8.0 * 200.0 / 1.0, Tolerance{1e-8, 1e-9}, "the flux after the edit, k dT / L");
}

TEST_CASE("ThermalReference_Determinism_RepeatedRunsAreBitIdentical", "[refmod][thermal][determinism]") {
    for (const Model& model : persistedModels()) {
        INFO(model.name);
        // five runs from five independently built documents
        std::vector<Fingerprint> prints;
        for (int i = 0; i < 5; ++i) {
            auto built = model.build();
            REQUIRE(built.has_value());
            Run run(std::move(built->document));
            REQUIRE(run.run().accepted());
            prints.push_back(fingerprintOf(run.run()));
        }
        for (const Fingerprint& p : prints) {
            CHECK(p == prints.front());
        }
        // and five solves of ONE document, mesh held: the chain after the mesh is deterministic by itself
        auto built = model.build();
        REQUIRE(built.has_value());
        Run run(std::move(built->document));
        const Fingerprint reference = fingerprintOf(run.run());
        for (int i = 0; i < 5; ++i) {
            run.rerun();
            REQUIRE(run.run().accepted());
            CHECK(fingerprintOf(run.run()) == reference);
        }
        CHECK(reference == prints.front());
        evidence(std::string{"determinism "} + model.name + ": 10 runs, " + std::to_string(reference.values) + " values, one fingerprint");
    }
}

TEST_CASE("ThermalReference_Determinism_OrderOfRunsDoesNotMatter", "[refmod][thermal][determinism]") {
    // The same three models in the opposite order, in the same process: no case may depend on another having run.
    const std::vector<Model> forward = persistedModels();
    std::vector<Fingerprint> forwardPrints;
    for (const Model& model : forward) {
        auto built = model.build();
        REQUIRE(built.has_value());
        Run run(std::move(built->document));
        REQUIRE(run.run().accepted());
        forwardPrints.push_back(fingerprintOf(run.run()));
    }
    for (std::size_t i = forward.size(); i-- > 0;) {
        auto built = forward[i].build();
        REQUIRE(built.has_value());
        Run run(std::move(built->document));
        REQUIRE(run.run().accepted());
        INFO(forward[i].name);
        CHECK(fingerprintOf(run.run()) == forwardPrints[i]);
    }
}
