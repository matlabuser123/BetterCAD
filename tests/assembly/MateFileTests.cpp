#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using Catch::Matchers::ContainsSubstring;

// P13-MATE-001 persistence. What is stored is intent: the kind, what it
// relates, and the value it carries. There is no solver, so there is nothing
// derived to store and nothing derived to leave out by accident.

namespace {

struct Assembly {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId a{};
    ComponentId b{};
};

Assembly makeAssembly() {
    Assembly m;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    m.sketch = require(m.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(m.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    m.part = require(m.document.addObject(std::move(*extrude)));
    m.a = require(assembly::createComponent(m.document, "Block1", {.part = m.part}));
    m.b = require(assembly::createComponent(m.document, "Block2", {.part = m.part}));
    return m;
}

MateTarget plane(ComponentId component) { return planeTarget(component, PlaneReference{}); }
MateTarget axis(ComponentId component) { return axisTarget(component, AxisReference{}); }
MateTarget face(ComponentId component, ObjectId feature) {
    return faceTarget(component, FaceName{.feature = feature, .face = {.role = FaceRole::EndCap}});
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.good());
    file << text;
}

/// How many times @p needle appears in @p text. Used where "absent" is the
/// wrong question: a mate's targets each name a component, so the honest
/// assertion is how many, not whether any.
std::size_t occurrences(const std::string& text, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

/// One object's entry, delimited by balancing braces. Scoped, because a
/// whole-file search for a word the format uses elsewhere proves nothing --
/// a lesson this project has now recorded twice.
std::string objectEntry(const std::string& text, std::string_view name) {
    const std::size_t at = text.find(std::format("\"name\": \"{}\"", name));
    REQUIRE(at != std::string::npos);
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

TEST_CASE("MateFile_RoundTripsEveryKind", "[assembly][mate][p13][io]") {
    TempDir dir;
    Assembly m = makeAssembly();

    std::vector<MateId> ids;
    ids.push_back(require(assembly::createMate(m.document, "Ground",
                                               {.type = MateType::Fixed, .component = m.a})));
    ids.push_back(require(assembly::createMate(m.document, "Touch",
                                               {.type = MateType::Coincident,
                                                .a = face(m.a, m.part), .b = plane(m.b)})));
    ids.push_back(require(assembly::createMate(m.document, "Bore",
                                               {.type = MateType::Concentric,
                                                .a = axis(m.a), .b = axis(m.b)})));
    ids.push_back(require(assembly::createMate(m.document, "Flat",
                                               {.type = MateType::Parallel,
                                                .a = plane(m.a), .b = plane(m.b)})));
    ids.push_back(require(assembly::createMate(m.document, "Square",
                                               {.type = MateType::Perpendicular,
                                                .a = axis(m.a), .b = axis(m.b)})));
    ids.push_back(require(assembly::createMate(m.document, "Gap",
                                               {.type = MateType::Distance, .a = plane(m.a),
                                                .b = plane(m.b), .distance = -25_mm})));
    ids.push_back(require(assembly::createMate(m.document, "Tilt",
                                               {.type = MateType::Angle, .a = plane(m.a),
                                                .b = plane(m.b), .angle = 30_deg, .suppressed = true})));
    REQUIRE(ids.size() == 7);

    const auto path = dir.path() / "mates.bcad";
    REQUIRE(io::saveDocument(m.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    CHECK(assembly::mates(*loaded) == ids);
    for (const MateId id : ids) {
        const assembly::Mate* before = assembly::findMate(m.document, id);
        const assembly::Mate* after = assembly::findMate(*loaded, id);
        REQUIRE(before != nullptr);
        REQUIRE(after != nullptr);
        INFO("mate: " << after->name());
        CHECK(after->definition() == before->definition());
        CHECK(after->dependencies() == before->dependencies());
        CHECK(after->contentEquals(*before));
    }
    // The value-bearing kinds keep their values exactly, sign included.
    CHECK(assembly::findMate(*loaded, ids[5])->definition().distance == -25_mm);
    CHECK(assembly::findMate(*loaded, ids[6])->definition().angle == 30_deg);
    CHECK(assembly::findMate(*loaded, ids[6])->definition().suppressed);

    const auto again = dir.path() / "mates-again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readText(path) == readText(again));
}

TEST_CASE("MateFile_WritesOnlyTheKeysItsKindCallsFor", "[assembly][mate][p13][io]") {
    // What validation requires is what is written, so a file cannot describe
    // a mate the model would refuse.
    TempDir dir;
    Assembly m = makeAssembly();
    REQUIRE(assembly::createMate(m.document, "Ground", {.type = MateType::Fixed, .component = m.a}).has_value());
    REQUIRE(assembly::createMate(m.document, "Flat", {.type = MateType::Parallel,
                                                      .a = plane(m.a), .b = plane(m.b)}).has_value());
    const auto path = dir.path() / "keys.bcad";
    REQUIRE(io::saveDocument(m.document, path).has_value());
    const std::string text = readText(path);

    const std::string fixed = objectEntry(text, "Ground");
    CHECK_THAT(fixed, ContainsSubstring("\"type\": \"fixed\""));
    CHECK_THAT(fixed, ContainsSubstring(std::format("\"component\": {}", m.a.value())));
    // One component, its own, and no targets at all.
    CHECK(occurrences(fixed, "\"component\"") == 1);
    CHECK_THAT(fixed, !ContainsSubstring("\"a\""));
    CHECK_THAT(fixed, !ContainsSubstring("\"distance\""));
    CHECK_THAT(fixed, !ContainsSubstring("\"suppressed\""));

    const std::string parallel = objectEntry(text, "Flat");
    CHECK_THAT(parallel, ContainsSubstring("\"type\": \"parallel\""));
    CHECK_THAT(parallel, ContainsSubstring("\"a\""));
    CHECK_THAT(parallel, ContainsSubstring("\"b\""));
    // Two components, one named by each target, and none of the mate's own:
    // a mate that relates geometry does not hold a component.
    CHECK(occurrences(parallel, "\"component\"") == 2);
    CHECK_THAT(parallel, ContainsSubstring(std::format("\"component\": {}", m.a.value())));
    CHECK_THAT(parallel, ContainsSubstring(std::format("\"component\": {}", m.b.value())));
    CHECK_THAT(parallel, !ContainsSubstring("\"distance\""));
    CHECK_THAT(parallel, !ContainsSubstring("\"angle\""));

    // No solver state, under any name it might take.
    for (const std::string_view forbidden : {"residual", "jacobian", "solved", "transform", "iteration"}) {
        INFO("forbidden key: " << forbidden);
        CHECK_THAT(text, !ContainsSubstring(std::string{forbidden}));
    }
    // And no format change.
    CHECK_THAT(text, ContainsSubstring("\"version\": 2"));
}

TEST_CASE("MateFile_ADocumentWithoutMatesIsUnchanged", "[assembly][mate][p13][io]") {
    TempDir dir;
    Assembly m = makeAssembly();
    const auto path = dir.path() / "nomates.bcad";
    REQUIRE(io::saveDocument(m.document, path).has_value());
    CHECK_THAT(readText(path), !ContainsSubstring("\"mate\""));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(assembly::mates(*loaded).empty());
}

TEST_CASE("MateFile_RefusesAMateTheModelWouldRefuse", "[assembly][mate][p13][io]") {
    // A file is not a way around validation. Loading runs the same checks
    // creation does, so an impossible mate is a parse failure rather than
    // something the rest of the system has to tolerate.
    TempDir dir;
    Assembly m = makeAssembly();
    REQUIRE(assembly::createMate(m.document, "Tilt", {.type = MateType::Angle, .a = plane(m.a),
                                                      .b = plane(m.b), .angle = 30_deg}).has_value());
    const auto path = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(m.document, path).has_value());
    const std::string good = readText(path);

    const auto loadEdited = [&](const std::string& from, const std::string& to) {
        std::string text = good;
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        const auto edited = dir.path() / "edited.bcad";
        writeText(edited, text);
        return io::loadDocument(edited);
    };

    SECTION("an angle outside the range the model allows") {
        auto loaded = loadEdited("\"angle\": 0.5235987755982988", "\"angle\": 4.0");
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("0 to 180 degrees"));
    }
    SECTION("a kind that does not exist") {
        auto loaded = loadEdited("\"type\": \"angle\"", "\"type\": \"tangent\"");
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("tangent"));
    }
    SECTION("an unknown key") {
        auto loaded = loadEdited("\"type\": \"angle\"", "\"residual\": 0.1,\n          \"type\": \"angle\"");
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("residual"));
    }
    SECTION("a target kind that does not match its geometry") {
        auto loaded = loadEdited("\"kind\": \"plane\"", "\"kind\": \"axis\"");
        REQUIRE_FALSE(loaded.has_value());
    }
}
