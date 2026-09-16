#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/Feature.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>

#include <map>
#include <set>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::refmodel;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace an = bettercad::test::analytic;
using an::pi;

namespace {

/// Builds one model of the catalogue.
Document build(const reference::ReferenceModelInfo& info) {
    auto document = reference::buildReferenceModel(info.kind);
    INFO((document ? std::string{} : document.error().message));
    REQUIRE(document.has_value());
    return std::move(*document);
}

/// The kinds of feature a model uses, e.g. {"extrude", "hole", "mirror"}.
std::set<std::string> featureKinds(const Document& document) {
    std::set<std::string> kinds;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const features::SolidFeature*>(&object) != nullptr) {
            kinds.insert(std::string{object.typeName()});
        }
    }
    return kinds;
}

} // namespace

TEST_CASE("ReferenceModel_AllModelsBuildInOneProcess", "[reference][all]") {
    // Every model built, regenerated and kept, one after another in one
    // process: nothing they share leaks between them.
    std::vector<Document> documents;
    std::vector<features::Regenerator> regenerators(reference::kReferenceModels.size());
    std::vector<reference::ModelFingerprint> prints;
    for (std::size_t i = 0; i < reference::kReferenceModels.size(); ++i) {
        const reference::ReferenceModelInfo& info = reference::kReferenceModels[i];
        INFO("model " << info.name);
        documents.push_back(build(info));
        CHECK(documents.back().name() == info.name);
        requireRegenerated(regenerators[i], documents.back());
        const features::ValidationReport validation = features::validateDocument(documents.back());
        CHECK(validation.valid());
        CHECK(validation.issues.empty());
        prints.push_back(requireFingerprint(documents.back(), regenerators[i]));
        checkSound(onlyBody(prints.back()));
    }
    CHECK(documents.size() == reference::kReferenceModels.size());

    // Each model on its own gives exactly the same result as in the row.
    for (std::size_t i = 0; i < reference::kReferenceModels.size(); ++i) {
        INFO("model " << reference::kReferenceModels[i].name);
        Document alone = build(reference::kReferenceModels[i]);
        features::Regenerator regenerator;
        requireRegenerated(regenerator, alone);
        CHECK(requireFingerprint(alone, regenerator) == prints[i]);
    }

    // The documents are distinct, and each one still holds its own model.
    std::set<std::string> names;
    for (std::size_t i = 0; i < documents.size(); ++i) {
        names.insert(documents[i].name());
        CHECK(requireFingerprint(documents[i], regenerators[i]) == prints[i]);
    }
    CHECK(names.size() == documents.size());
}

TEST_CASE("ReferenceModel_AllModelsStressRegression", "[reference][all]") {
    // Five rounds of building every model, regenerating it and throwing it
    // away: no crash, no drift, no state carried over.
    constexpr int kRounds = 5;
    std::vector<reference::ModelFingerprint> first;
    for (int round = 0; round < kRounds; ++round) {
        INFO("round " << round + 1 << " of " << kRounds);
        for (std::size_t i = 0; i < reference::kReferenceModels.size(); ++i) {
            const reference::ReferenceModelInfo& info = reference::kReferenceModels[i];
            INFO("model " << info.name);
            Document document = build(info);
            features::Regenerator regenerator;
            requireRegenerated(regenerator, document);
            const reference::ModelFingerprint print = requireFingerprint(document, regenerator);
            checkSound(onlyBody(print));
            if (round == 0) {
                first.push_back(print);
            } else {
                CHECK(print == first[i]);
            }
        }
    }
}

TEST_CASE("ReferenceModel_AllModelsRegenerateAfterAChange", "[reference][all]") {
    // Each model has a main dimension; changing it rebuilds the model and
    // gives a different, still sound body, and putting it back gives the
    // original to rounding.
    for (const reference::ReferenceModelInfo& info : reference::kReferenceModels) {
        INFO("model " << info.name << ", " << info.mainParameter << " = " << info.mainParameterMm << " mm");
        Document document = build(info);
        features::Regenerator regenerator;
        requireRegenerated(regenerator, document);
        const reference::ModelFingerprint baseline = requireFingerprint(document, regenerator);

        const ObjectId item = document.findByName(info.mainParameter).value_or(ObjectId{});
        REQUIRE(item.isValid());
        const ParameterId parameter = document.asParameter(item).value_or(ParameterId{});
        REQUIRE(parameter.isValid());
        const double original = document.parameters().find(parameter)->siValue();
        REQUIRE(document.setParameterValue(parameter, info.mainParameterMm * units::mm).has_value());
        requireRegenerated(regenerator, document);
        const reference::ModelFingerprint changed = requireFingerprint(document, regenerator);
        checkSound(onlyBody(changed));
        CHECK_FALSE(changed == baseline);

        REQUIRE(document.setParameterSiValue(parameter, dimensions::length, original).has_value());
        requireRegenerated(regenerator, document);
        const reference::FingerprintDifference back =
            reference::compare(baseline, requireFingerprint(document, regenerator));
        CHECK(back.sameStructure);
        CHECK(back.volumeAreaRelative <= 1e-12);
        CHECK(back.positionMm <= 1e-9);
    }
}

TEST_CASE("ReferenceModel_FeatureCoverage", "[reference][all]") {
    // Between them the reference models use every P11 feature. The matrix is
    // printed for the evidence.
    std::map<std::string, std::set<std::string>> matrix;
    std::set<std::string> all;
    for (const reference::ReferenceModelInfo& info : reference::kReferenceModels) {
        const Document document = build(info);
        matrix[std::string{info.name}] = featureKinds(document);
        all.insert(matrix[std::string{info.name}].begin(), matrix[std::string{info.name}].end());
    }
    for (const auto& [model, kinds] : matrix) {
        std::string line;
        for (const std::string& kind : kinds) {
            line += (line.empty() ? "" : ", ") + kind;
        }
        INFO(model << ": " << line);
        CHECK_FALSE(kinds.empty());
    }
    for (const std::string_view feature : {"extrude", "revolve", "chamfer", "fillet", "hole", "linear_pattern",
                                           "circular_pattern", "mirror", "sweep", "loft"}) {
        INFO("feature " << feature);
        CHECK(all.contains(std::string{feature}));
    }
    // Each model is a chain, not a single feature.
    for (const auto& [model, kinds] : matrix) {
        INFO(model);
        CHECK(kinds.size() >= 2);
    }
}

TEST_CASE("ReferenceModel_SavedModelsMatchTheBuilders", "[reference][all][io]") {
    // examples/models/reference/*.bcad are the models as their builders make
    // them, saved before regeneration. They are kept in the repository so the
    // CLI and other tools have real parts to work on, and they must stay in
    // step with the builders: saving each builder's document reproduces its
    // file byte for byte, and loading the file gives the same model back.
    const std::filesystem::path directory = std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "reference";
    TempDir dir;
    for (const reference::ReferenceModelInfo& info : reference::kReferenceModels) {
        INFO("model " << info.name);
        const std::filesystem::path committed = directory / (std::string{info.fileStem} + ".bcad");
        REQUIRE(std::filesystem::exists(committed));
        Document built = build(info);
        const auto path = dir.path() / (std::string{info.fileStem} + ".bcad");
        REQUIRE(io::saveDocument(built, path).has_value());
        CHECK(readFile(path) == readFile(committed));

        auto loaded = io::loadDocument(committed);
        REQUIRE(loaded.has_value());
        CHECK(equivalent(*loaded, built));
        features::Regenerator fromFile;
        features::Regenerator fromBuilder;
        requireRegenerated(fromFile, *loaded);
        requireRegenerated(fromBuilder, built);
        CHECK(requireFingerprint(*loaded, fromFile) == requireFingerprint(built, fromBuilder));
    }
}

TEST_CASE("ReferenceModel_AnalyticToolkit", "[reference][all]") {
    // The independent geometry the expectations are built from, against
    // closed forms.
    SECTION("a disc and its moments") {
        const std::vector<an::Loop> disc{an::circle({0.0, 0.0}, 10.0)};
        CHECK_THAT(an::moment(disc, 0, 0), WithinRel(pi * 100.0, 1e-14));
        CHECK_THAT(an::moment(disc, 1, 0), WithinAbs(0.0, 1e-12));
        CHECK_THAT(an::perimeter(disc), WithinRel(2.0 * pi * 10.0, 1e-14));
        // A disc of radius r centred at R, turned about the v axis: a torus
        // of volume 2 pi^2 R r^2 and area 4 pi^2 R r (Pappus).
        const std::vector<an::Loop> tube{an::circle({20.0, 0.0}, 3.0)};
        const an::Solid torus = an::revolved(tube);
        CHECK_THAT(torus.volume, WithinRel(2.0 * pi * pi * 20.0 * 9.0, 1e-13));
        CHECK_THAT(torus.area, WithinRel(4.0 * pi * pi * 20.0 * 3.0, 1e-13));
        CHECK_THAT(torus.centroid[2], WithinAbs(0.0, 1e-12));
    }
    SECTION("a fillet's section") {
        const double r = 5.0;
        an::Path p({0.0, 0.0});
        p.to({r, 0.0}).arcTo({r, r}, r, 270.0, 180.0);
        const std::vector<an::Loop> spandrel{p.close()};
        CHECK_THAT(an::moment(spandrel, 0, 0), WithinRel(an::filletArea(r), 1e-13));
        CHECK_THAT(an::moment(spandrel, 1, 0) / an::moment(spandrel, 0, 0),
                   WithinRel(an::filletCentroid(r), 1e-12));
        CHECK_THAT(an::moment(spandrel, 0, 1) / an::moment(spandrel, 0, 0),
                   WithinRel(an::filletCentroid(r), 1e-12));
    }
    SECTION("a cylinder, a cone and a rectangle") {
        const std::vector<an::Loop> cylinder{an::polygon({{0.0, 0.0}, {4.0, 0.0}, {4.0, 10.0}, {0.0, 10.0}})};
        const an::Solid solid = an::revolved(cylinder);
        CHECK_THAT(solid.volume, WithinRel(pi * 16.0 * 10.0, 1e-14));
        CHECK_THAT(solid.area, WithinRel(2.0 * pi * 4.0 * 10.0 + 2.0 * pi * 16.0, 1e-14));
        CHECK_THAT(solid.centroid[2], WithinRel(5.0, 1e-13));
        // A cone: V = pi r^2 h / 3, centroid h/4 up.
        const std::vector<an::Loop> cone{an::polygon({{0.0, 0.0}, {6.0, 0.0}, {0.0, 12.0}})};
        const an::Solid pointed = an::revolved(cone);
        CHECK_THAT(pointed.volume, WithinRel(pi * 36.0 * 12.0 / 3.0, 1e-13));
        CHECK_THAT(pointed.centroid[2], WithinRel(3.0, 1e-12));
        // A rectangle's own moments.
        const std::vector<an::Loop> rectangle{an::polygon({{1.0, 2.0}, {5.0, 2.0}, {5.0, 8.0}, {1.0, 8.0}})};
        CHECK_THAT(an::moment(rectangle, 0, 0), WithinRel(24.0, 1e-14));
        CHECK_THAT(an::moment(rectangle, 1, 0) / 24.0, WithinRel(3.0, 1e-14));
        CHECK_THAT(an::moment(rectangle, 0, 1) / 24.0, WithinRel(5.0, 1e-14));
    }
    SECTION("a region with a hole") {
        const std::vector<an::Loop> plate{an::polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}),
                                          an::circle({5.0, 5.0}, 2.0, /*clockwise=*/true)};
        CHECK_THAT(an::moment(plate, 0, 0), WithinRel(100.0 - 4.0 * pi, 1e-13));
        CHECK_THAT(an::moment(plate, 1, 0) / an::moment(plate, 0, 0), WithinRel(5.0, 1e-12));
    }
    SECTION("integration along a length") {
        CHECK_THAT(an::integrate([](double t) { return t * t; }, 0.0, 3.0), WithinRel(9.0, 1e-14));
        CHECK_THAT(an::integrate([](double t) { return 2.0 * t + 1.0; }, 1.0, 4.0), WithinRel(18.0, 1e-14));
    }
}
