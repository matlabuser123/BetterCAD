#include "features/FeatureTestSupport.hpp"
#include "support/ConfigurationModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BoxFamilyModel;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-PARAM-002 in the native format: configurations, their overrides and
// the active one.

namespace {

constexpr double kRel = 1e-12;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

Error loadError(const std::string& text) {
    const auto loaded = io::documentFromJson(text);
    REQUIRE_FALSE(loaded.has_value());
    UNSCOPED_INFO(loaded.error().message);
    return loaded.error();
}

} // namespace

TEST_CASE("Configurations_SaveLoadRegeneratesTheSameFamily", "[io][configurations][p12][acceptance]") {
    // The real round trip: build, save, destroy, load, regenerate, compare.
    TempDir dir;
    const auto path = dir.path() / "family.bcad";

    for (const std::string name : {"Small", "Medium", "Large"}) {
        DYNAMIC_SECTION("active: " << name) {
            BoxFamilyModel m;
            const ConfigurationId active = m.doc.configurations().findByName(name)->id();
            m.activate(active);
            Regenerator before;
            REQUIRE(requireReport(before, m.doc).succeeded());
            const double volume = volumeMm3(before, m.box);

            const Document expected = m.doc.clone();
            REQUIRE(io::saveDocument(m.doc, path).has_value());
            m.doc = Document("Closed");
            auto loaded = io::loadDocument(path);
            REQUIRE(loaded.has_value());
            CHECK(equivalent(expected, *loaded));

            // The configuration came back whole, and it is still the active one.
            CHECK(loaded->configurations().size() == 3);
            REQUIRE(loaded->activeConfiguration().has_value());
            CHECK(loaded->configurations().find(*loaded->activeConfiguration())->name() == name);
            CHECK(bits(loaded->effectiveParameterValue(m.width)->siValue) ==
                  bits(expected.effectiveParameterValue(m.width)->siValue));

            Regenerator after;
            REQUIRE(requireReport(after, *loaded).succeeded());
            CHECK(bits(volumeMm3(after, m.box)) == bits(volume));

            // And it can still be switched after loading.
            const ConfigurationId other = loaded->configurations().findByName("Large")->id();
            REQUIRE(loaded->setActiveConfiguration(other).has_value());
            REQUIRE(requireReport(after, *loaded).succeeded());
            CHECK_THAT(volumeMm3(after, m.box),
                       WithinRel(BoxFamilyModel::expectedVolumeMm3(160.0), kRel));
        }
    }
}

TEST_CASE("Configurations_TheBaseConfigurationSavesAndLoads", "[io][configurations][p12]") {
    // No active configuration is the base configuration, and it survives as
    // such rather than becoming the first one.
    TempDir dir;
    BoxFamilyModel m;
    REQUIRE(m.doc.activeConfiguration() == std::nullopt);
    const auto path = dir.path() / "base.bcad";
    REQUIRE(io::saveDocument(m.doc, path).has_value());

    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"configurations\""));
    CHECK_THAT(text, !ContainsSubstring("\"active\""));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->activeConfiguration() == std::nullopt);
    CHECK(loaded->configurations().size() == 3);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, *loaded).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.box), WithinRel(BoxFamilyModel::expectedVolumeMm3(100.0), kRel));
}

TEST_CASE("Configurations_AreStoredAsTransparentJson", "[io][configurations][p12]") {
    BoxFamilyModel m;
    m.activate(m.medium);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    // Named, not numbered, so the file stays readable; overrides carry the
    // parameter's ID and an SI value, the dimension being the parameter's.
    CHECK_THAT(*text, ContainsSubstring(R"("configurations": {
    "active": "Medium",
    "defined": [
      {
        "id": 6,
        "name": "Small",
        "overrides": [
          {
            "parameter": 1,
            "si_value": 0.04
          }
        ]
      },)"));
}

TEST_CASE("Configurations_FilesWrittenBeforeThisMilestoneLoadUnchanged",
          "[io][configurations][p12][regression]") {
    // Backward compatibility. A document with no configurations writes no
    // "configurations" key at all, so a file from before P12-PARAM-002 is
    // byte for byte what it was, and loads with its own meaning.
    TempDir dir;
    Document plain{"Plain"};
    const ParameterId width = plain.createParameter("width", 100_mm, units::mm).value();
    const ParameterId height = plain.createParameter("height", 1_mm, units::mm).value();
    REQUIRE(plain.setParameterExpression(height, "width / 2").has_value());

    const auto text = io::documentToJson(plain);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, !ContainsSubstring("configurations"));

    auto loaded = io::documentFromJson(*text);
    REQUIRE(loaded.has_value());
    CHECK(loaded->configurations().empty());
    CHECK(loaded->activeConfiguration() == std::nullopt);
    // Every parameter means what it meant: no configuration, so the
    // effective value is the parameter's own.
    CHECK(bits(loaded->effectiveParameterValue(width)->siValue) == bits(0.1));
    const auto again = io::documentToJson(*loaded);
    REQUIRE(again.has_value());
    CHECK(*again == *text);
}

TEST_CASE("Configurations_MalformedDataIsRejectedWithTheJsonPath", "[io][configurations][p12]") {
    BoxFamilyModel m;
    m.activate(m.small);
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());

    SECTION("an override of a parameter that is not there") {
        const Error error = loadError(replaceOnce(good, R"("parameter": 1,)", R"("parameter": 987,)"));
        CHECK(error.code == ErrorCode::ParseError);
        CHECK(error.message ==
              "configurations.defined[0].overrides[0].parameter: no parameter with ID 987");
    }
    SECTION("an override of a driven parameter") {
        // `height` is driven by an expression, so no configuration may set it.
        const Error error = loadError(replaceOnce(good, R"("parameter": 1,)", R"("parameter": 2,)"));
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, ContainsSubstring("is driven by the expression 'width / 2'"));
    }
    SECTION("an active configuration that does not exist") {
        const Error error = loadError(replaceOnce(good, R"("active": "Small")", R"("active": "Gone")"));
        CHECK(error.code == ErrorCode::ParseError);
        CHECK(error.message == "configurations.active: no configuration named 'Gone'");
    }
    SECTION("a duplicate configuration name") {
        const Error error = loadError(replaceOnce(good, R"("name": "Medium")", R"("name": "Small")"));
        CHECK(error.code == ErrorCode::AlreadyExists);
        CHECK_THAT(error.message, ContainsSubstring("a configuration named 'Small' already exists"));
    }
    SECTION("an invalid configuration name") {
        const Error error = loadError(replaceOnce(good, R"("name": "Small")", R"("name": "")"));
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("configurations.defined[0]"));
    }
    SECTION("an unknown field") {
        const Error error = loadError(replaceOnce(good, R"("name": "Small",)", R"("name": "Small", "tag": 1,)"));
        CHECK(error.code == ErrorCode::ParseError);
        CHECK(error.message == "configurations.defined[0].tag: unknown field");
    }
    SECTION("a configuration ID of zero") {
        const Error error = loadError(replaceOnce(good, R"("id": 6,)", R"("id": 0,)"));
        CHECK(error.code == ErrorCode::ParseError);
        CHECK(error.message == "configurations.defined[0].id: a configuration needs a valid ID");
    }
    SECTION("a non-finite override") {
        const Error error = loadError(replaceOnce(good, R"("si_value": 0.04)", R"("si_value": 1e999)"));
        CHECK_FALSE(error.message.empty());
    }
}

TEST_CASE("Configurations_SavingIsDeterministic", "[io][configurations][p12][determinism]") {
    BoxFamilyModel first;
    BoxFamilyModel second;
    first.activate(first.large);
    second.activate(second.large);
    const auto a = io::documentToJson(first.doc);
    const auto b = io::documentToJson(second.doc);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);

    // Saving after a round trip through a file gives the same text again.
    TempDir dir;
    const auto path = dir.path() / "stable.bcad";
    REQUIRE(io::saveDocument(first.doc, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const auto again = io::documentToJson(*loaded);
    REQUIRE(again.has_value());
    CHECK(*again == *a);
}
