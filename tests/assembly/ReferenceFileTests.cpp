#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/Uuid.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/core/document/ReferenceResolver.hpp>
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
using Catch::Matchers::ContainsSubstring;

// P13-REF-001 persistence. What is stored is identity: the document's UUID
// and the object's ID inside it. A locator is stored beside that identity,
// never as it.

namespace {

struct PartDocument {
    Document document;
    ObjectId sketch{};
    ObjectId part{};

    explicit PartDocument(std::string name) : document(std::move(name)) {}
};

PartDocument makePart(std::string documentName = "Parts", std::string partName = "Block") {
    PartDocument p{std::move(documentName)};
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    p.sketch = require(p.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        std::move(partName), {.profile = SketchId::fromValue(p.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    p.part = require(p.document.addObject(std::move(*extrude)));
    return p;
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

/// The text of one object's entry, found by name and delimited by balancing
/// braces.
///
/// Scoping an "is absent" assertion to the entry is not fussiness: every
/// .bcad file has a top-level "document" object, so searching the whole file
/// for that word finds it every time and proves nothing. P13-COMP-001
/// recorded this same over-broad-search defect against "placement"; this is
/// it a second time, caught the same way -- by running the test.
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

class OpenDocuments final : public ReferenceResolver {
public:
    void add(const Document& document) { documents_.push_back(&document); }
    [[nodiscard]] const Document* candidate(const ObjectReference& reference) const override {
        if (!reference.document) {
            return nullptr;
        }
        for (const Document* document : documents_) {
            if (document->id() == *reference.document) {
                return document;
            }
        }
        return nullptr;
    }

private:
    std::vector<const Document*> documents_;
};

} // namespace

TEST_CASE("ReferenceFile_AnInternalPartIsStillABareNumber", "[assembly][objectref][p13][io]") {
    // The compatibility guarantee. Widening `part` to a reference did not
    // change a single byte of a component that names a local object, so
    // every file written before P13-REF-001 is still written identically and
    // still reads.
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(p.document, "Block1", {.part = p.part}).has_value());

    const auto path = dir.path() / "internal.bcad";
    REQUIRE(io::saveDocument(p.document, path).has_value());
    const std::string text = readText(path);

    const std::string entry = objectEntry(text, "Block1");
    CHECK_THAT(entry, ContainsSubstring(std::format("\"part\": {}", p.part.value())));
    // Scoped to the component: the file's own top-level "document" object is
    // not evidence about the reference.
    CHECK_THAT(entry, !ContainsSubstring("\"document\""));
    CHECK_THAT(entry, !ContainsSubstring("\"hint\""));
    CHECK_THAT(entry, !ContainsSubstring("\"object\""));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const auto ids = assembly::components(*loaded);
    REQUIRE(ids.size() == 1);
    const assembly::Component* component = assembly::findComponent(*loaded, ids.front());
    REQUIRE(component != nullptr);
    CHECK(isInternal(component->definition().part));
    CHECK(component->definition().part.object == p.part);
}

TEST_CASE("ReferenceFile_AnExternalPartStoresIdentityAndALocator", "[assembly][objectref][p13][io]") {
    TempDir dir;
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There", "ForeignBlock");
    const ObjectReference external{there.document.id(), there.part, "parts/there.bcad"};
    const ComponentId id = require(assembly::createComponent(here.document, "Away", {.part = external}));

    const auto path = dir.path() / "external.bcad";
    REQUIRE(io::saveDocument(here.document, path).has_value());
    const std::string text = readText(path);

    // Identity is the UUID, written as text.
    CHECK_THAT(text, ContainsSubstring(there.document.id().value().toString()));
    CHECK_THAT(text, ContainsSubstring("\"hint\": \"parts/there.bcad\""));
    CHECK_THAT(text, ContainsSubstring(std::format("\"object\": {}", there.part.value())));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const assembly::Component* component = assembly::findComponent(*loaded, id);
    REQUIRE(component != nullptr);
    // Everything comes back, the locator included.
    CHECK(component->definition().part == external);
    CHECK(sameTarget(component->definition().part, external));

    // And it still resolves to the same object.
    OpenDocuments resolver;
    resolver.add(there.document);
    auto part = assembly::resolveComponentPart(*loaded, id, &resolver);
    REQUIRE(part.has_value());

    const auto again = dir.path() / "external-again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readText(path) == readText(again));
}

TEST_CASE("ReferenceFile_LoadsWithTheSourceMissingAndResolvesWhenItReturns", "[assembly][objectref][p13][io]") {
    // ADR-003: unresolved is a state, not an error at load. A document whose
    // external part is nowhere to be found still opens, still says what is
    // unresolved, and resolves once the source is available.
    TempDir dir;
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There", "ForeignBlock");
    const ComponentId id = require(assembly::createComponent(
        here.document, "Away", {.part = {there.document.id(), there.part, "parts/there.bcad"}}));

    const auto path = dir.path() / "dangling.bcad";
    REQUIRE(io::saveDocument(here.document, path).has_value());

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    OpenDocuments nothing;
    const auto unresolved = assembly::unresolvedComponents(*loaded, &nothing);
    REQUIRE(unresolved.size() == 1);
    CHECK(unresolved.front().component == id);
    CHECK(unresolved.front().state == ReferenceState::DocumentUnavailable);

    OpenDocuments returned;
    returned.add(there.document);
    CHECK(assembly::unresolvedComponents(*loaded, &returned).empty());
    auto part = assembly::resolveComponentPart(*loaded, id, &returned);
    REQUIRE(part.has_value());
}

TEST_CASE("ReferenceFile_ALocatorChangeDoesNotChangeIdentity", "[assembly][objectref][p13][io]") {
    TempDir dir;
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There");
    const ComponentId id = require(assembly::createComponent(
        here.document, "Away", {.part = {there.document.id(), there.part, "parts/there.bcad"}}));

    const auto path = dir.path() / "moved.bcad";
    REQUIRE(io::saveDocument(here.document, path).has_value());

    // Edit the stored locator, as moving the source file would.
    std::string text = readText(path);
    const std::string before = "\"hint\": \"parts/there.bcad\"";
    REQUIRE(text.find(before) != std::string::npos);
    text.replace(text.find(before), before.size(), "\"hint\": \"archive/2026/there.bcad\"");
    const auto edited = dir.path() / "moved-edited.bcad";
    writeText(edited, text);

    auto loaded = io::loadDocument(edited);
    REQUIRE(loaded.has_value());
    const assembly::Component* component = assembly::findComponent(*loaded, id);
    REQUIRE(component != nullptr);
    CHECK(component->definition().part.hint == "archive/2026/there.bcad");
    // The locator moved; the target did not.
    CHECK(sameTarget(component->definition().part, ObjectReference{there.document.id(), there.part}));

    OpenDocuments resolver;
    resolver.add(there.document);
    CHECK(assembly::resolveComponentPart(*loaded, id, &resolver).has_value());
}

TEST_CASE("ReferenceFile_RejectsMalformedReferences", "[assembly][objectref][p13][io]") {
    TempDir dir;
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There");
    REQUIRE(assembly::createComponent(here.document, "Away",
                                      {.part = {there.document.id(), there.part, "parts/there.bcad"}})
                .has_value());
    const auto path = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(here.document, path).has_value());
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

    SECTION("a document identity that is not a UUID") {
        auto loaded = loadEdited(there.document.id().value().toString(), "not-a-uuid");
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("UUID"));
    }
    SECTION("a nil document identity") {
        auto loaded = loadEdited(there.document.id().value().toString(), "00000000-0000-0000-0000-000000000000");
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("UUID"));
    }
    SECTION("an unknown key inside the reference") {
        auto loaded = loadEdited("\"hint\":", "\"path\": \"x\",\n          \"hint\":");
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("path"));
    }
    SECTION("a locator that is not a string") {
        auto loaded = loadEdited("\"hint\": \"parts/there.bcad\"", "\"hint\": 7");
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring("hint"));
    }
}
