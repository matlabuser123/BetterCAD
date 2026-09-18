#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Configurations.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-PARAM-002 at the document level: what a configuration is, what it
// refuses, and how it reaches the equations.

namespace {

constexpr double kRel = 1e-12;

/// A document with `width` free and `height = width / 2`, plus an empty
/// configuration: the smallest model that shows a configuration driving an
/// equation.
struct Model {
    Document doc{"Configured"};
    ParameterId width, height, angle;
    ConfigurationId big;

    Model() {
        width = doc.createParameter("width", 100_mm, units::mm).value();
        height = doc.createParameter("height", 1_mm, units::mm).value();
        angle = doc.createParameter("tilt", 30_deg, units::deg).value();
        REQUIRE(doc.setParameterExpression(height, "width / 2").has_value());
        big = doc.createConfiguration("Big").value();
    }

    [[nodiscard]] double effective(ParameterId id) const {
        return doc.effectiveParameterValue(id)->siValue;
    }
    [[nodiscard]] double stored(ParameterId id) const { return doc.parameters().find(id)->siValue(); }
};

} // namespace

TEST_CASE("Configuration_OverridesTheValueInForceAndNotTheBase", "[configurations][p12][acceptance]") {
    Model m;
    REQUIRE(m.doc.setConfigurationOverride(m.big, m.width, 240_mm).has_value());

    // Until it is activated, nothing has changed.
    CHECK(m.effective(m.width) == 0.1);
    REQUIRE(m.doc.setActiveConfiguration(m.big).has_value());
    CHECK(m.effective(m.width) == 0.24);

    // The canonical value never moved: that is what makes switching back
    // exact, and what keeps the document's own state the engineering intent.
    CHECK(m.stored(m.width) == 0.1);
    REQUIRE(m.doc.setActiveConfiguration(std::nullopt).has_value());
    CHECK(m.effective(m.width) == 0.1);
}

TEST_CASE("Configuration_DrivesTheEquationsThatFollowFromIt", "[configurations][p12][acceptance]") {
    // The point of the milestone: a configuration sets `width` only, and
    // `height = width / 2` follows without being duplicated anywhere.
    Model m;
    REQUIRE(m.doc.setConfigurationOverride(m.big, m.width, 240_mm).has_value());

    const ParameterEvaluationReport base = evaluateParameterExpressions(m.doc);
    CHECK(base.succeeded());
    CHECK_THAT(m.stored(m.height), WithinRel(0.05, kRel));

    REQUIRE(m.doc.setActiveConfiguration(m.big).has_value());
    const ParameterEvaluationReport big = evaluateParameterExpressions(m.doc);
    CHECK(big.succeeded());
    CHECK_THAT(m.stored(m.height), WithinRel(0.12, kRel));
    CHECK(big.changed == std::vector<ParameterId>{m.height});

    // And back again, exactly.
    REQUIRE(m.doc.setActiveConfiguration(std::nullopt).has_value());
    CHECK(evaluateParameterExpressions(m.doc).succeeded());
    CHECK_THAT(m.stored(m.height), WithinRel(0.05, kRel));
}

TEST_CASE("Configuration_ChainsOfEquationsFollowTheOverride", "[configurations][p12][acceptance]") {
    // A = 100 mm; B = A / 2; C = B + 10 mm; D = 2 * C, with a configuration
    // setting A. By hand at A = 100: B = 50, C = 60, D = 120; at A = 60:
    // B = 30, C = 40, D = 80.
    Document doc{"Chain"};
    const ParameterId d = doc.createParameter("D", 1_mm, units::mm).value();
    const ParameterId c = doc.createParameter("C", 1_mm, units::mm).value();
    const ParameterId b = doc.createParameter("B", 1_mm, units::mm).value();
    const ParameterId a = doc.createParameter("A", 100_mm, units::mm).value();
    REQUIRE(doc.setParameterExpression(d, "2 * C").has_value());
    REQUIRE(doc.setParameterExpression(c, "B + 10 mm").has_value());
    REQUIRE(doc.setParameterExpression(b, "A / 2").has_value());

    const auto value = [&](ParameterId id) { return doc.parameters().find(id)->siValue(); };
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK_THAT(value(b), WithinRel(0.05, kRel));
    CHECK_THAT(value(c), WithinRel(0.06, kRel));
    CHECK_THAT(value(d), WithinRel(0.12, kRel));

    const ConfigurationId narrow = doc.createConfiguration("Narrow").value();
    REQUIRE(doc.setConfigurationOverride(narrow, a, 60_mm).has_value());
    REQUIRE(doc.setActiveConfiguration(narrow).has_value());
    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    REQUIRE(report.succeeded());
    CHECK_THAT(value(b), WithinRel(0.03, kRel));
    CHECK_THAT(value(c), WithinRel(0.04, kRel));
    CHECK_THAT(value(d), WithinRel(0.08, kRel));
    // The whole chain moved, in dependency order, not creation order.
    CHECK(report.evaluated == std::vector<ParameterId>{b, c, d});
}

TEST_CASE("Configuration_RefusesWhatItCannotMean", "[configurations][p12][acceptance]") {
    Model m;
    const auto message = [](const auto& result) {
        REQUIRE_FALSE(result.has_value());
        return result.error();
    };

    SECTION("a driven parameter cannot be overridden") {
        // Its value comes from its expression; an override would be a second
        // answer to the same question.
        const Error error = message(m.doc.setConfigurationOverride(m.big, m.height, 10_mm));
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, ContainsSubstring("is driven by the expression 'width / 2'"));
    }
    SECTION("an override must have the parameter's dimension") {
        const Error error = message(m.doc.setConfigurationOverride(m.big, m.width, 30_deg));
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK(error.message ==
              "parameter 'width' is length, so a configuration cannot give it a value that is angle");
    }
    SECTION("an unknown parameter") {
        const Error error = message(
            m.doc.setConfigurationOverride(m.big, ParameterId::fromValue(999), 10_mm));
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "parameter:999 does not exist");
    }
    SECTION("an unknown configuration") {
        const Error error = message(
            m.doc.setConfigurationOverride(ConfigurationId::fromValue(999), m.width, 10_mm));
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "configuration:999 does not exist");
    }
    SECTION("a non-finite value") {
        const Error error = message(m.doc.setConfigurationOverride(
            m.big, m.width, DimensionedValue{dimensions::length, std::numeric_limits<double>::infinity()}));
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("must be finite"));
    }
    SECTION("a duplicate name") {
        const Error error = message(m.doc.createConfiguration("Big"));
        CHECK(error.code == ErrorCode::AlreadyExists);
        CHECK(error.message == "a configuration named 'Big' already exists");
    }
    SECTION("an empty or invalid name") {
        CHECK(message(m.doc.createConfiguration("")).code == ErrorCode::InvalidArgument);
        CHECK(message(m.doc.createConfiguration("2 sizes")).code == ErrorCode::InvalidArgument);
        CHECK(message(m.doc.createConfiguration(" Big")).code == ErrorCode::InvalidArgument);
    }
    SECTION("activating a configuration that does not exist") {
        const Error error = message(m.doc.setActiveConfiguration(ConfigurationId::fromValue(999)));
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(m.doc.activeConfiguration() == std::nullopt);
    }
    // Every refusal left the document alone.
    CHECK(m.doc.configurations().size() == 1);
    CHECK(m.doc.configurations().find(m.big)->overrides().empty());
}

TEST_CASE("Configuration_ADeletedParameterLeavesEveryConfiguration", "[configurations][p12]") {
    // A configuration cannot override what is not there, so removing a
    // parameter removes its overrides. The alternative -- a dangling
    // override -- would be a reference to something that no longer exists.
    Model m;
    const ConfigurationId other = m.doc.createConfiguration("Other").value();
    REQUIRE(m.doc.setConfigurationOverride(m.big, m.width, 240_mm).has_value());
    REQUIRE(m.doc.setConfigurationOverride(other, m.width, 60_mm).has_value());
    REQUIRE(m.doc.setConfigurationOverride(m.big, m.angle, 45_deg).has_value());

    REQUIRE(m.doc.removeParameter(m.angle).has_value());
    CHECK(m.doc.configurations().find(m.big)->overrides().size() == 1);
    CHECK(m.doc.configurations().find(m.big)->overrides(m.width));
    CHECK(m.doc.configurations().find(other)->overrides().size() == 1);
}

TEST_CASE("Configuration_TableKeepsNamesUniqueAndIdsStable", "[configurations][p12]") {
    Model m;
    const ConfigurationId other = m.doc.createConfiguration("Other").value();
    CHECK(other != m.big);
    CHECK(m.doc.configurations().findByName("Big")->id() == m.big);
    CHECK(m.doc.configurations().findByName("Missing") == nullptr);

    // Renaming keeps uniqueness.
    CHECK_FALSE(m.doc.renameConfiguration(other, "Big").has_value());
    REQUIRE(m.doc.renameConfiguration(other, "Bigger").has_value());
    CHECK(m.doc.configurations().findByName("Bigger")->id() == other);
    // Renaming to the same name is not a change.
    CHECK(m.doc.renameConfiguration(other, "Bigger").value() == false);

    // Removing the active one falls back to the base configuration.
    REQUIRE(m.doc.setActiveConfiguration(other).has_value());
    REQUIRE(m.doc.removeConfiguration(other).has_value());
    CHECK(m.doc.activeConfiguration() == std::nullopt);
    CHECK(m.doc.configurations().size() == 1);
}

TEST_CASE("Configuration_ClearingAnOverrideRestoresTheBaseValue", "[configurations][p12]") {
    Model m;
    REQUIRE(m.doc.setConfigurationOverride(m.big, m.width, 240_mm).has_value());
    REQUIRE(m.doc.setActiveConfiguration(m.big).has_value());
    CHECK(m.effective(m.width) == 0.24);

    CHECK(m.doc.clearConfigurationOverride(m.big, m.width).value() == true);
    CHECK(m.effective(m.width) == 0.1);
    // Clearing one that is not there is not a change.
    CHECK(m.doc.clearConfigurationOverride(m.big, m.width).value() == false);
}

TEST_CASE("Configuration_IdsComeFromTheDocumentsOneAllocator", "[configurations][p12]") {
    // Configuration IDs share the document's allocator, so no ID is ever
    // reused by a parameter, an object or another configuration.
    Model m;
    const ObjectId beforeId = ObjectId::fromValue(m.doc.lastAllocatedId());
    const ConfigurationId fresh = m.doc.createConfiguration("Fresh").value();
    CHECK(fresh.value() > beforeId.value());
    const ParameterId after = m.doc.createParameter("after", 1_mm, units::mm).value();
    CHECK(after.value() > fresh.value());

    // A configuration is not a document object: it has no place in the item
    // list and cannot be found by the object name lookup.
    const std::vector<ObjectId> items = m.doc.itemIds();
    CHECK(std::ranges::find(items, ObjectId::fromValue(fresh.value())) == items.end());
    CHECK(m.doc.findByName("Fresh") == std::nullopt);
}
