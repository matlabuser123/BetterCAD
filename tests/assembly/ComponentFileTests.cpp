#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;

// P13-COMP-001 persistence. ADR-002 claims components need no change to the
// .bcad format: they go through the existing objects[] envelope as a new
// `type`. These tests prove that claim against the actual bytes rather than
// taking the ADR's word for it.

namespace {

struct PartDocument {
    Document document{"Parts"};
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

} // namespace

TEST_CASE("ComponentFile_RoundTripsEveryCanonicalRelationship", "[assembly][component][p13][io]") {
    // Not "the geometry came back": the component's identity, the part it
    // names, its suppression and its dependency edge.
    TempDir dir;
    PartDocument p = makePart();
    const ComponentId a = require(assembly::createComponent(p.document, "Block1", {.part = p.part}));
    const ComponentId b = require(assembly::createComponent(p.document, "Block2", {.part = p.part}));
    const ComponentId c =
        require(assembly::createComponent(p.document, "Block3", {.part = p.part, .suppressed = true}));

    const auto path = dir.path() / "assembly.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    // The same three components, with the same IDs, in the same order.
    CHECK(assembly::components(*loaded) == std::vector<ComponentId>{a, b, c});
    CHECK(loaded->objectCount() == p.document.objectCount());
    CHECK(loaded->lastAllocatedId() == p.document.lastAllocatedId());

    for (const ComponentId id : {a, b, c}) {
        INFO("component " << id);
        const assembly::Component* before = assembly::findComponent(p.document, id);
        const assembly::Component* after = assembly::findComponent(*loaded, id);
        REQUIRE(before != nullptr);
        REQUIRE(after != nullptr);
        CHECK(after->name() == before->name());
        CHECK(after->definition() == before->definition());
        // The reference still names the part, by the same ID.
        CHECK(after->definition().part == p.part);
        CHECK(loaded->findObject(after->definition().part.object) != nullptr);
        // And the dependency edge came back with it.
        CHECK(after->dependencies() == std::vector<ObjectId>{p.part});
        CHECK(after->contentEquals(*before));
    }
    // Suppression is engineering intent and survives.
    CHECK(assembly::findComponent(*loaded, c)->definition().suppressed);
    CHECK_FALSE(assembly::findComponent(*loaded, a)->definition().suppressed);

    // Saving what was loaded gives the same bytes.
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readText(again) == readText(path));
}

TEST_CASE("ComponentFile_UsesTheExistingObjectEnvelopeAndNoNewKey", "[assembly][component][p13][io]") {
    // ADR-002's central claim, checked against the file itself.
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(p.document, "Block1", {.part = p.part}).has_value());
    const auto path = dir.path() / "assembly.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    const std::string text = readText(path);
    INFO(text);

    // The format version is untouched...
    CHECK_THAT(text, ContainsSubstring("\"version\": 2"));
    CHECK_THAT(text, ContainsSubstring("\"format\": \"bettercad-document\""));
    // ...there is no new top-level key for assemblies...
    CHECK_THAT(text, !ContainsSubstring("\"assembly\""));
    CHECK_THAT(text, !ContainsSubstring("\"assemblies\""));
    CHECK_THAT(text, !ContainsSubstring("\"components\""));
    // ...and the component is an ordinary entry in objects[], with the
    // envelope every other kind uses.
    CHECK_THAT(text, ContainsSubstring("\"type\": \"component\""));
    CHECK_THAT(text, ContainsSubstring("\"part\":"));
    // What the COMPONENT entry itself holds. Scoped to that entry rather
    // than the whole file, because keys like "placement" legitimately
    // belong to other kinds -- it is a sketch's plane frame, and has been
    // since P0. A blanket search over the file would fail for a reason that
    // has nothing to do with components.
    const auto entry = text.find("\"type\": \"component\"");
    REQUIRE(entry != std::string::npos);
    const std::string component = text.substr(entry);
    INFO("component entry: " << component);
    // Nothing anticipating external references (ADR-003)...
    CHECK_THAT(component, !ContainsSubstring("source_document"));
    CHECK_THAT(component, !ContainsSubstring("path"));
    CHECK_THAT(component, !ContainsSubstring("uuid"));
    CHECK_THAT(component, !ContainsSubstring("document"));
    // ...nothing anticipating transforms (ADR-005)...
    CHECK_THAT(component, !ContainsSubstring("transform"));
    CHECK_THAT(component, !ContainsSubstring("placement"));
    CHECK_THAT(component, !ContainsSubstring("frame"));
    // ...and no suppression key for an unsuppressed component.
    CHECK_THAT(component, !ContainsSubstring("suppressed"));
    // The whole of its data is the one reference it needs.
    CHECK_THAT(component, ContainsSubstring("\"part\": 2"));
}

TEST_CASE("ComponentFile_ADocumentWithoutComponentsIsUnchanged", "[assembly][component][p13][io]") {
    // The compatibility claim: adding the component kind must not change a
    // byte of a document that has none.
    TempDir dir;
    PartDocument p = makePart();
    const auto before = dir.path() / "part.bcad";
    REQUIRE(io::saveDocument(p.document, before).has_value());
    const std::string partOnly = readText(before);
    CHECK_THAT(partOnly, !ContainsSubstring("component"));

    // Add a component, save, remove it, save again: back to the same bytes.
    const ComponentId id = require(assembly::createComponent(p.document, "Block1", {.part = p.part}));
    const auto withComponent = dir.path() / "with.bcad";
    REQUIRE(io::saveDocument(p.document, withComponent).has_value());
    CHECK(readText(withComponent) != partOnly);

    REQUIRE(assembly::removeComponent(p.document, id).has_value());
    const auto after = dir.path() / "after.bcad";
    REQUIRE(io::saveDocument(p.document, after).has_value());
    // The only difference a removed component can leave is the allocator
    // high-water mark, which must never go backwards.
    CHECK_THAT(readText(after), !ContainsSubstring("component"));
    CHECK(p.document.lastAllocatedId() == id.value());
}

TEST_CASE("ComponentFile_RejectsMalformedComponentData", "[assembly][component][p13][io]") {
    // A file is a trust boundary: a component whose data is wrong must be a
    // parse error naming its path, not a silently empty reference.
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(p.document, "Block1", {.part = p.part}).has_value());
    const auto path = dir.path() / "assembly.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    const std::string good = readText(path);

    const auto writeAndLoad = [&](const std::string& text) {
        const auto broken = dir.path() / "broken.bcad";
        std::ofstream out(broken, std::ios::binary);
        out << text;
        out.close();
        return io::loadDocument(broken);
    };

    SECTION("a part that is not an ID") {
        std::string text = good;
        const auto at = text.find("\"part\":");
        REQUIRE(at != std::string::npos);
        text.replace(at, std::string("\"part\": 2").size(), "\"part\": \"x\"");
        auto loaded = writeAndLoad(text);
        CHECK_FALSE(loaded.has_value());
    }
    SECTION("an unknown key in the component data") {
        std::string text = good;
        const auto at = text.find("\"part\":");
        REQUIRE(at != std::string::npos);
        text.insert(at, "\"transform\": 1, ");
        auto loaded = writeAndLoad(text);
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("transform"));
    }
}
