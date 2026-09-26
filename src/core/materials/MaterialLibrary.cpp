#include <bettercad/core/materials/MaterialLibrary.hpp>

#include <algorithm>
#include <format>

namespace bettercad::materials {

namespace {

constexpr std::string_view kBuiltInLibrary = "bettercad";

} // namespace

/// Grants the table access to LibraryMaterial's constructor, exactly as
/// MetricThreadTable does for MetricThread.
struct MaterialLibraryTable {
    /// The built-in entries.
    ///
    /// METADATA ONLY, and that is the point (ADR-028): a property value needs a
    /// recorded source, so the milestone that adds density and modulus records
    /// where each number came from. Putting plausible numbers here to make the
    /// library look finished is exactly how wrong values enter a model.
    ///
    /// The designations and standard numbers below are labels for these
    /// materials, not measurements of them:
    ///   6061-T6           wrought aluminium alloy, extrusions to ASTM B221
    ///   S235JR            non-alloy structural steel, EN 10025-2
    ///   304               austenitic stainless, plate/sheet/strip to ASTM A240
    ///   ABS               no single designating standard, so none is cited
    ///
    /// The last entry is deliberate: it exercises an absent standard rather
    /// than inventing one, in the spirit of core/standards/, which records what
    /// it does not know.
    static constexpr LibraryMaterial kEntries[] = {
        {kBuiltInLibrary, "al-6061-t6", 1, "Aluminium 6061-T6", "ASTM B221", "Aluminium Alloy",
         "Metadata only: this entry carries no property values."},
        {kBuiltInLibrary, "steel-s235jr", 1, "Steel S235JR", "EN 10025-2", "Carbon Steel",
         "Metadata only: this entry carries no property values."},
        {kBuiltInLibrary, "stainless-304", 1, "Stainless Steel 304", "ASTM A240",
         "Stainless Steel", "Metadata only: this entry carries no property values."},
        {kBuiltInLibrary, "abs", 1, "ABS", "", "Polymer",
         "Metadata only: this entry carries no property values. ABS has no single "
         "designating standard, so none is cited."},
    };
};

std::string toString(const MaterialLibraryKey& key) {
    return std::format("{}/{} rev {}", key.library, key.entry, key.revision);
}

MaterialLibraryKey LibraryMaterial::key() const {
    return MaterialLibraryKey{std::string{library_}, std::string{entry_}, revision_};
}

std::string_view builtInLibraryName() noexcept { return kBuiltInLibrary; }

std::span<const LibraryMaterial> builtInMaterials() noexcept {
    return std::span<const LibraryMaterial>{MaterialLibraryTable::kEntries};
}

Result<LibraryMaterial> findLibraryMaterial(std::string_view library, std::string_view entry) {
    const std::span<const LibraryMaterial> entries = builtInMaterials();
    const auto found = std::ranges::find_if(entries, [&](const LibraryMaterial& candidate) {
        return candidate.library() == library && candidate.entry() == entry;
    });
    if (found == entries.end()) {
        return makeError(ErrorCode::NotFound,
                         std::format("no material library entry '{}/{}'", library, entry));
    }
    return *found;
}

Result<LibraryMaterial> findLibraryMaterial(const MaterialLibraryKey& key) {
    Result<LibraryMaterial> entry = findLibraryMaterial(key.library, key.entry);
    if (!entry) {
        return entry;
    }
    if (entry->revision() != key.revision) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("material library entry '{}/{}' is at revision {}, not {}",
                                     key.library, key.entry, entry->revision(), key.revision));
    }
    return entry;
}

} // namespace bettercad::materials
