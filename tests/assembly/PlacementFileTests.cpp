#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-XFORM-001 persistence. ADR-005 says the file holds placement INTENT
// and never a solved transform, so these tests check the bytes for what is
// present and, just as importantly, for what must be absent.

namespace {

constexpr double kTolerance = 1e-12;

struct PartDocument {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
};

PartDocument makePart() {
    PartDocument p;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    p.sketch = require(p.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(p.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    p.part = require(p.document.addObject(std::move(*extrude)));
    return p;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/// The text of the component entry only, so that an "absent" assertion is
/// about the component and not about some other object that happens to use
/// the same word. (P13-COMP-001 learned this the hard way: "placement" is a
/// sketch's plane frame and has been a key since P0.)
std::string componentEntry(const std::string& text, std::string_view name) {
    const std::size_t at = text.find(std::format("\"name\": \"{}\"", name));
    REQUIRE(at != std::string::npos);
    // Back up to the brace that opens this object, then scan forward
    // balancing braces to its match. Looking for "}," instead would miss the
    // last object of the array, which ends with a bare brace.
    const std::size_t start = text.rfind('{', at);
    REQUIRE(start != std::string::npos);
    int depth = 0;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(start, i - start + 1);
            }
        }
    }
    FAIL("no closing brace for the object named " << name);
    return {};
}

} // namespace

TEST_CASE("PlacementFile_RoundTripsIntentIncludingItsParameters", "[assembly][placement][p13][io]") {
    TempDir dir;
    PartDocument p = makePart();
    const ParameterId offset = require(p.document.createParameter("offset", 25_mm, units::mm));
    const ComponentPlacement literal{.translation = {10_mm, -20_mm, 30_mm},
                                     .rotation = {15_deg, 0_deg, 90_deg}};
    const ComponentPlacement driven{.translationParameters = {offset, std::nullopt, std::nullopt}};
    const ComponentId a = require(assembly::createComponent(p.document, "Block1", {.part = p.part, .placement = literal}));
    const ComponentId b = require(assembly::createComponent(p.document, "Block2", {.part = p.part, .placement = driven}));

    const auto path = dir.path() / "placed.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    // The intent comes back exactly, parameters included.
    const assembly::Component* loadedA = assembly::findComponent(*loaded, a);
    const assembly::Component* loadedB = assembly::findComponent(*loaded, b);
    REQUIRE(loadedA != nullptr);
    REQUIRE(loadedB != nullptr);
    CHECK(loadedA->definition().placement == literal);
    CHECK(loadedB->definition().placement == driven);
    CHECK(loadedB->definition().placement.translationParameters[0] == offset);

    // And so does what the intent means.
    const RigidTransform3D before = require(assembly::placementOf(p.document, a));
    const RigidTransform3D after = require(assembly::placementOf(*loaded, a));
    CHECK(before == after);
    // The driven one still follows its parameter in the loaded document.
    CHECK_THAT(require(assembly::placementOf(*loaded, b)).translationPart().x.si(), WithinAbs(0.025, kTolerance));

    // Re-saving is byte-identical, so the round trip loses and invents
    // nothing.
    const auto again = dir.path() / "placed-again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readText(path) == readText(again));
}

TEST_CASE("PlacementFile_WritesNothingForAnUnmovedComponent", "[assembly][placement][p13][io]") {
    // The compatibility guarantee: a component that has not been placed is
    // written exactly as P13-COMP-001 wrote it, so files from before this
    // milestone are unchanged, and a file written now is readable by the
    // same reader.
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(p.document, "Block1", {.part = p.part}).has_value());

    const auto path = dir.path() / "unmoved.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    const std::string entry = componentEntry(readText(path), "Block1");

    CHECK_THAT(entry, ContainsSubstring("\"type\": \"component\""));
    CHECK_THAT(entry, !ContainsSubstring("placement"));
    CHECK_THAT(entry, !ContainsSubstring("\"x\""));
    CHECK_THAT(entry, !ContainsSubstring("\"rz\""));

    // And it loads back as the identity rather than as nothing.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const auto ids = assembly::components(*loaded);
    REQUIRE(ids.size() == 1);
    CHECK(isIdentity(assembly::findComponent(*loaded, ids.front())->definition().placement));
    CHECK(require(assembly::placementOf(*loaded, ids.front())).isTranslation());
}

TEST_CASE("PlacementFile_HoldsIntentAndNeverASolvedTransform", "[assembly][placement][p13][io]") {
    // ADR-005's central claim, checked against the bytes: the file says what
    // was asked for, never where the answer came out.
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(
                p.document, "Block1",
                {.part = p.part, .placement = {.translation = {10_mm, 0_mm, 0_mm}, .rotation = {0_deg, 0_deg, 90_deg}}})
                .has_value());

    const auto path = dir.path() / "intent.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    const std::string text = readText(path);
    const std::string entry = componentEntry(text, "Block1");

    // The intent is there, in SI, with the same six keys a datum offset uses.
    CHECK_THAT(entry, ContainsSubstring("\"placement\""));
    CHECK_THAT(entry, ContainsSubstring("\"x\": 0.01"));
    CHECK_THAT(entry, ContainsSubstring("\"rz\": 1.5707963267948966"));

    // The derived transform is not, under any of the names it could take.
    for (const std::string_view forbidden : {"transform", "matrix", "solved", "world", "position", "basis"}) {
        INFO("forbidden key: " << forbidden);
        CHECK_THAT(entry, !ContainsSubstring(std::string{forbidden}));
    }
    // No format change either: still version 1, still the objects[] envelope.
    CHECK_THAT(text, ContainsSubstring("\"version\": 2"));
    CHECK_THAT(text, !ContainsSubstring("\"assembly\""));
}

TEST_CASE("PlacementFile_RejectsMalformedPlacement", "[assembly][placement][p13][io]") {
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(p.document, "Block1",
                                      {.part = p.part, .placement = {.translation = {10_mm, 0_mm, 0_mm}}})
                .has_value());
    const auto path = dir.path() / "broken.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    const std::string good = readText(path);

    const auto writeAndLoad = [&](const std::string& text) {
        const auto broken = dir.path() / "edited.bcad";
        std::ofstream out(broken, std::ios::binary);
        out << text;
        out.close();
        return io::loadDocument(broken);
    };

    SECTION("a translation that is not a number") {
        std::string text = good;
        const std::size_t at = text.find("\"x\": 0.01");
        REQUIRE(at != std::string::npos);
        text.replace(at, std::string("\"x\": 0.01").size(), "\"x\": \"ten\"");
        auto loaded = writeAndLoad(text);
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("placement"));
    }
    SECTION("an unknown key inside the placement") {
        std::string text = good;
        const std::size_t at = text.find("\"x\": 0.01");
        REQUIRE(at != std::string::npos);
        text.replace(at, std::string("\"x\": 0.01").size(), "\"scale\": 2.0,\n          \"x\": 0.01");
        auto loaded = writeAndLoad(text);
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("scale"));
    }
    SECTION("a missing key") {
        std::string text = good;
        const std::size_t at = text.find("\"rz\"");
        REQUIRE(at != std::string::npos);
        const std::size_t end = text.find('\n', at);
        text.erase(at, end - at + 1);
        auto loaded = writeAndLoad(text);
        REQUIRE_FALSE(loaded.has_value());
    }
}
