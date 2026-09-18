#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::refmodel;
using Catch::Matchers::WithinRel;

// P12-REF-001 across all six production models at once: the things that must
// hold for every one of them, rather than per part. The per-model files hold
// the independent geometry; this file holds the contracts.

namespace {

/// The models this milestone adds, with the parameter each test drives.
struct P12Model {
    reference::ReferenceModelKind kind;
    const char* name;
    const char* fileStem;
    const char* parameter;
    double changedMm;
};

constexpr std::array kP12Models{
    P12Model{reference::ReferenceModelKind::MotorMount, "MotorMount", "motor_mount", "width", 150.0},
    P12Model{reference::ReferenceModelKind::GearboxCover, "GearboxCover", "gearbox_cover", "width", 96.0},
    P12Model{reference::ReferenceModelKind::ManifoldTube, "ManifoldTube", "manifold_tube", "drop", 65.0},
    P12Model{reference::ReferenceModelKind::TransitionDuct, "TransitionDuct", "transition_duct", "throat_r", 33.0},
    P12Model{reference::ReferenceModelKind::IndexPlate, "IndexPlate", "index_plate", "plate_r", 99.0},
    P12Model{reference::ReferenceModelKind::RibbedBracket, "RibbedBracket", "ribbed_bracket", "wall_h", 75.0},
};

Document build(const P12Model& model) {
    auto document = reference::buildReferenceModel(model.kind);
    INFO((document ? std::string{} : document.error().message));
    REQUIRE(document.has_value());
    return std::move(*document);
}

/// Returning a parameter to its old value must return the model to its old
/// shape. These are the SAME bounds AllModelsTests.cpp already holds every
/// reference model to for the same change-and-restore over the same
/// parameters, and a weaker bound here would quietly exempt the new models
/// from a gate the old ones pass. An earlier revision of this file did set
/// them looser, reasoning from a twisted sweep's 1.5e-7 fit error; that
/// reasoning was wrong, because the fit is deterministic and reproduces
/// exactly when the parameter returns. Restoration is exact.
constexpr double kRelRestored = 1e-12;
constexpr double kPositionRestoredMm = 1e-9;

ParameterId parameterOf(const Document& doc, const char* name) {
    const Parameter* found = doc.parameters().findByName(name);
    REQUIRE(found != nullptr);
    return found->id();
}

} // namespace

TEST_CASE("ReferenceModel_P12ModelsBuildValidateAndRegenerate", "[reference][p12][all][acceptance]") {
    // Every production model builds, validates without an issue, and gives
    // one sound result body.
    for (const P12Model& model : kP12Models) {
        DYNAMIC_SECTION(model.name) {
            Document doc = build(model);
            const features::ValidationReport validation = features::validateDocument(doc);
            for (const features::ValidationIssue& issue : validation.issues) {
                UNSCOPED_INFO(features::toString(issue.severity) << " " << features::toString(issue.check)
                                                                 << ": " << issue.message);
            }
            CHECK(validation.valid());
            CHECK(validation.issues.empty());
            CHECK(validation.bodies.size() == 1);

            features::Regenerator regenerator;
            requireRegenerated(regenerator, doc);
            checkSound(onlyBody(requireFingerprint(doc, regenerator)));
        }
    }
}

TEST_CASE("ReferenceModel_P12ModelsSaveLoadAndRegenerate", "[reference][p12][all][io][acceptance]") {
    // The real round trip for each part: build, save, destroy, load,
    // regenerate, compare. The fingerprint compares IDs, names, feature
    // count, topology, volume, area, centroid and bounds, all exactly.
    TempDir dir;
    for (const P12Model& model : kP12Models) {
        DYNAMIC_SECTION(model.name) {
            Document built = build(model);
            features::Regenerator before;
            requireRegenerated(before, built);
            const auto expected = requireFingerprint(built, before);

            const auto path = dir.path() / (std::string{model.fileStem} + ".bcad");
            REQUIRE(io::saveDocument(built, path).has_value());
            auto loaded = io::loadDocument(path);
            REQUIRE(loaded.has_value());
            // Engineering intent, not merely the solid: parameters,
            // expressions, configurations, sketches, features and
            // references all compare equal.
            CHECK(equivalent(*loaded, built));
            CHECK(loaded->itemIds() == built.itemIds());
            CHECK(loaded->configurations().size() == built.configurations().size());
            CHECK(loaded->activeConfiguration() == built.activeConfiguration());

            features::Regenerator after;
            requireRegenerated(after, *loaded);
            CHECK(requireFingerprint(*loaded, after) == expected);
        }
    }
}

TEST_CASE("ReferenceModel_P12ModelsAreDeterministic", "[reference][p12][all][determinism]") {
    // Regenerating twice, and building a second document from scratch, give
    // the same model exactly -- the fingerprint compares every value.
    for (const P12Model& model : kP12Models) {
        DYNAMIC_SECTION(model.name) {
            Document first = build(model);
            features::Regenerator a;
            requireRegenerated(a, first);
            const auto once = requireFingerprint(first, a);
            requireRegenerated(a, first);
            CHECK(requireFingerprint(first, a) == once);

            Document second = build(model);
            features::Regenerator b;
            requireRegenerated(b, second);
            CHECK(requireFingerprint(second, b) == once);
        }
    }
}

TEST_CASE("ReferenceModel_P12ModelsRegenerateAfterAChangeAndBack", "[reference][p12][all][acceptance]") {
    // A driving parameter moved and restored. The model must change, and
    // must come back: nothing downstream may keep a stale body.
    for (const P12Model& model : kP12Models) {
        DYNAMIC_SECTION(model.name) {
            Document doc = build(model);
            features::Regenerator regenerator;
            requireRegenerated(regenerator, doc);
            const auto before = requireFingerprint(doc, regenerator);
            const ParameterId parameter = parameterOf(doc, model.parameter);
            const double original = doc.parameters().find(parameter)->siValue();

            REQUIRE(doc.setParameterValue(parameter, model.changedMm * units::mm).has_value());
            requireRegenerated(regenerator, doc);
            const auto changed = requireFingerprint(doc, regenerator);
            INFO("parameter " << model.parameter << " -> " << model.changedMm << " mm");
            CHECK(onlyBody(changed).volumeMm3 != onlyBody(before).volumeMm3);
            checkSound(onlyBody(changed));

            REQUIRE(doc.setParameterSiValue(parameter, dimensions::length, original).has_value());
            requireRegenerated(regenerator, doc);
            const auto difference = reference::compare(before, requireFingerprint(doc, regenerator));
            CHECK(difference.sameStructure);
            CHECK(difference.volumeAreaRelative <= kRelRestored);
            CHECK(difference.positionMm <= kPositionRestoredMm);
        }
    }
}

TEST_CASE("ReferenceModel_P12ModelsUndoAndRedoAProductionEdit", "[reference][p12][all][undo][acceptance]") {
    // The same edit through the command history: undo restores the model and
    // redo reapplies it, with no stale body left behind either way.
    for (const P12Model& model : kP12Models) {
        DYNAMIC_SECTION(model.name) {
            Document doc = build(model);
            CommandHistory history;
            features::Regenerator regenerator;
            requireRegenerated(regenerator, doc);
            const auto before = requireFingerprint(doc, regenerator);
            const ParameterId parameter = parameterOf(doc, model.parameter);

            REQUIRE(history
                        .execute(doc, ModifyParameterCommand::setValue(parameter,
                                                                       Length::fromSi(model.changedMm / 1000.0)))
                        .has_value());
            requireRegenerated(regenerator, doc);
            const auto changed = requireFingerprint(doc, regenerator);
            CHECK(onlyBody(changed).volumeMm3 != onlyBody(before).volumeMm3);

            REQUIRE(history.undo(doc).has_value());
            requireRegenerated(regenerator, doc);
            auto back = reference::compare(before, requireFingerprint(doc, regenerator));
            CHECK(back.sameStructure);
            CHECK(back.volumeAreaRelative <= kRelRestored);

            REQUIRE(history.redo(doc).has_value());
            requireRegenerated(regenerator, doc);
            auto again = reference::compare(changed, requireFingerprint(doc, regenerator));
            CHECK(again.sameStructure);
            CHECK(again.volumeAreaRelative <= kRelRestored);
        }
    }
}

TEST_CASE("ReferenceModel_P12ModelsExportStepAndReadBack", "[reference][p12][all][io][step][acceptance]") {
    // STEP is a geometry interchange path, not an engineering one: what is
    // checked is that the kernel's own reader finds one valid solid of the
    // same volume and bounds. Nothing here claims that the feature tree,
    // the parameters or the configurations survive, because they do not.
    TempDir dir;
    for (const P12Model& model : kP12Models) {
        DYNAMIC_SECTION(model.name) {
            Document doc = build(model);
            features::Regenerator regenerator;
            requireRegenerated(regenerator, doc);
            const auto body = onlyBody(requireFingerprint(doc, regenerator));
            checkStepExport(doc, dir.path() / (std::string{model.fileStem} + ".step"), body.volumeMm3,
                            body.minMm, body.maxMm);
        }
    }
}

TEST_CASE("ReferenceModel_P12ModelsBuiltTogetherMatchThoseBuiltAlone", "[reference][p12][all][determinism]") {
    // Hidden global state would show as a model built in company differing
    // from the same model built on its own.
    std::vector<reference::ModelFingerprint> alone;
    for (const P12Model& model : kP12Models) {
        Document doc = build(model);
        features::Regenerator regenerator;
        requireRegenerated(regenerator, doc);
        alone.push_back(requireFingerprint(doc, regenerator));
    }

    std::vector<Document> together;
    std::vector<std::unique_ptr<features::Regenerator>> regenerators;
    for (const P12Model& model : kP12Models) {
        together.push_back(build(model));
        regenerators.push_back(std::make_unique<features::Regenerator>());
    }
    for (std::size_t i = 0; i < together.size(); ++i) {
        requireRegenerated(*regenerators[i], together[i]);
    }
    for (std::size_t i = 0; i < together.size(); ++i) {
        INFO("model " << kP12Models[i].name);
        CHECK(requireFingerprint(together[i], *regenerators[i]) == alone[i]);
    }
}
