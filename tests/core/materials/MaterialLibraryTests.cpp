#include <bettercad/core/materials/MaterialLibrary.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

using namespace bettercad;
using Catch::Matchers::ContainsSubstring;
using materials::LibraryMaterial;
using materials::MaterialLibraryKey;

namespace {

std::vector<std::string> entryKeys() {
    std::vector<std::string> keys;
    for (const LibraryMaterial& entry : materials::builtInMaterials()) {
        keys.emplace_back(entry.entry());
    }
    return keys;
}

} // namespace

TEST_CASE("MaterialLibrary_EnumeratesEveryEntryInTheSameOrderEveryTime") {
    const std::vector<std::string> first = entryKeys();
    REQUIRE_FALSE(first.empty());
    // Ten more traversals: the order is the table's, so nothing about container
    // iteration, hashing or address order can reorder it between calls.
    for (int pass = 0; pass < 10; ++pass) {
        REQUIRE(entryKeys() == first);
    }
}

TEST_CASE("MaterialLibrary_EveryEntryHasADistinctKeyWithinItsLibrary") {
    std::vector<std::string> keys = entryKeys();
    std::ranges::sort(keys);
    REQUIRE(std::ranges::adjacent_find(keys) == keys.end());
}

TEST_CASE("MaterialLibrary_EveryEntryNamesTheBuiltInLibraryAndARevisionFromOne") {
    for (const LibraryMaterial& entry : materials::builtInMaterials()) {
        INFO("entry " << std::string{entry.entry()});
        REQUIRE(entry.library() == materials::builtInLibraryName());
        REQUIRE(entry.revision() >= 1);
        REQUIRE_FALSE(entry.entry().empty());
        REQUIRE_FALSE(entry.designation().empty());
    }
}

TEST_CASE("MaterialLibrary_FindsAnEntryByItsKeyAndNotByItsDesignation") {
    const Result<LibraryMaterial> found =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(found);
    REQUIRE(found->designation() == "Aluminium 6061-T6");
    REQUIRE(found->family() == "Aluminium Alloy");

    // The designation is a label, not a key. Looking one up by it must not work,
    // or a library reword would silently change what resolves.
    const Result<LibraryMaterial> byDesignation =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "Aluminium 6061-T6");
    REQUIRE_FALSE(byDesignation);
    REQUIRE(byDesignation.error().code == ErrorCode::NotFound);
}

TEST_CASE("MaterialLibrary_AnUnknownEntryIsNotFoundRatherThanSubstituted") {
    for (const auto& [library, entry] :
         std::vector<std::pair<std::string, std::string>>{{"bettercad", "unobtainium"},
                                                          {"bettercad", ""},
                                                          {"no-such-library", "al-6061-t6"},
                                                          {"", "al-6061-t6"}}) {
        INFO("looking up '" << library << "/" << entry << "'");
        const Result<LibraryMaterial> found = materials::findLibraryMaterial(library, entry);
        REQUIRE_FALSE(found);
        REQUIRE(found.error().code == ErrorCode::NotFound);
        // No nearest match, no first entry, no generic steel.
        REQUIRE_THAT(found.error().message, ContainsSubstring(entry));
    }
}

TEST_CASE("MaterialLibrary_AKeyRoundTripsToTheEntryItCameFrom") {
    for (const LibraryMaterial& entry : materials::builtInMaterials()) {
        const MaterialLibraryKey key = entry.key();
        INFO("key " << materials::toString(key));
        const Result<LibraryMaterial> found = materials::findLibraryMaterial(key);
        REQUIRE(found);
        REQUIRE(*found == entry);
    }
}

TEST_CASE("MaterialLibrary_AKeyAtTheWrongRevisionIsReportedRatherThanResolved") {
    MaterialLibraryKey key = materials::findLibraryMaterial("bettercad", "al-6061-t6")->key();
    const int current = key.revision;
    key.revision = current + 7;

    const Result<LibraryMaterial> found = materials::findLibraryMaterial(key);
    REQUIRE_FALSE(found);
    REQUIRE(found.error().code == ErrorCode::FailedPrecondition);
    // The diagnostic has to say which revision exists and which was asked for,
    // or a caller cannot offer the user the choice ADR-025 requires.
    REQUIRE_THAT(found.error().message, ContainsSubstring(std::to_string(current)));
    REQUIRE_THAT(found.error().message, ContainsSubstring(std::to_string(current + 7)));
}

TEST_CASE("MaterialLibrary_KeysCompareByContentAndOrderDeterministically") {
    const MaterialLibraryKey a{"bettercad", "al-6061-t6", 1};
    const MaterialLibraryKey b{"bettercad", "al-6061-t6", 1};
    const MaterialLibraryKey laterRevision{"bettercad", "al-6061-t6", 2};
    const MaterialLibraryKey otherEntry{"bettercad", "steel-s235jr", 1};

    REQUIRE(a == b);
    REQUIRE(a != laterRevision);
    REQUIRE(a != otherEntry);
    REQUIRE(a < laterRevision);
    REQUIRE(a < otherEntry);
}

TEST_CASE("MaterialLibrary_DescribesAKeyWithItsLibraryEntryAndRevision") {
    REQUIRE(materials::toString(MaterialLibraryKey{"bettercad", "al-6061-t6", 2}) ==
            "bettercad/al-6061-t6 rev 2");
}

TEST_CASE("MaterialLibrary_CarriesNoPropertyValuesAtThisMilestone") {
    // P15-MAT-001 is identity and metadata. A property value needs a recorded
    // source (ADR-028), so the absence here is deliberate, and this fails the
    // day someone adds a plausible-looking number without one. The notes field
    // is what says so.
    for (const LibraryMaterial& entry : materials::builtInMaterials()) {
        INFO("entry " << std::string{entry.entry()});
        REQUIRE_THAT(std::string{entry.notes()}, ContainsSubstring("no property values"));
    }
}
