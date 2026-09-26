#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>

#include <compare>
#include <span>
#include <string>
#include <string_view>

// The built-in material library: immutable reference data, no geometry, on the
// core/standards/ pattern (ADR-025, ADR-028).
//
// A library entry is NOT a document object and has no ObjectId. Using one
// IMPORTS it: a document-owned material object is created from the entry and
// the entry's values become the document's own (features::importLibraryMaterial).
// Nothing looks a library entry up at load time or at solve time, so a library
// correction, a library addition, a newer build or a missing library cannot
// change what a saved document computes.
//
// P15-MAT-001 establishes identity and metadata only. The entries below carry
// no property values -- no density, no modulus, no conductivity -- because a
// property value needs a recorded source (ADR-028) and belongs to the milestone
// that adds it. The designations and standard numbers here are labels, not
// measurements.
namespace bettercad::materials {

/// Names one entry of one material library, and the library's own revision of
/// that entry.
///
/// Owns its strings deliberately. A document records this key as the provenance
/// of an imported material (ADR-025), so it is persisted state, and persisted
/// state must not hold a pointer into anything -- which a string_view is. The
/// ADR sketches the key with views; that is right for naming a compiled-in
/// entry and wrong for a field a file round-trips.
struct MaterialLibraryKey {
    /// Which library, e.g. "bettercad".
    std::string library;
    /// Stable entry key within that library, e.g. "al-6061-t6". NOT the
    /// designation: the designation is an engineering label a library may
    /// reword, and this must not change when it does.
    std::string entry;
    /// The library's own revision of that entry, from 1. A later revision of
    /// the same entry is the same material, corrected.
    int revision = 0;

    friend bool operator==(const MaterialLibraryKey&, const MaterialLibraryKey&) = default;
    friend auto operator<=>(const MaterialLibraryKey&, const MaterialLibraryKey&) = default;
};

/// "bettercad/al-6061-t6 rev 2", for diagnostics and provenance.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string toString(const MaterialLibraryKey& key);

/// One entry of a built-in material library.
///
/// Immutable: there are no setters and the entries are compiled-in constants,
/// so "a document must not mutate the library" needs no rule enforcing it --
/// there is nothing to mutate. Get one from builtInMaterials() or
/// findLibraryMaterial().
class BETTERCAD_CORE_EXPORT LibraryMaterial {
public:
    /// Which library this came from and which revision of the entry.
    [[nodiscard]] MaterialLibraryKey key() const;

    [[nodiscard]] std::string_view library() const noexcept { return library_; }
    [[nodiscard]] std::string_view entry() const noexcept { return entry_; }
    [[nodiscard]] int revision() const noexcept { return revision_; }

    /// The engineering label, e.g. "Aluminium 6061-T6". Not identity: two
    /// materials may carry the same designation (ADR-025).
    [[nodiscard]] std::string_view designation() const noexcept { return designation_; }
    /// The standard the designation is taken from, e.g. "ASTM B221". Empty
    /// when the entry cites none.
    [[nodiscard]] std::string_view standard() const noexcept { return standard_; }
    /// A grouping label, e.g. "Aluminium Alloy". Free text, not a taxonomy.
    [[nodiscard]] std::string_view family() const noexcept { return family_; }
    /// What this entry does and does not say. Empty when there is nothing to
    /// add.
    [[nodiscard]] std::string_view notes() const noexcept { return notes_; }

    friend bool operator==(const LibraryMaterial&, const LibraryMaterial&) = default;

private:
    friend struct MaterialLibraryTable;

    constexpr LibraryMaterial(std::string_view library, std::string_view entry, int revision,
                              std::string_view designation, std::string_view standard,
                              std::string_view family, std::string_view notes) noexcept
        : library_(library), entry_(entry), designation_(designation), standard_(standard),
          family_(family), notes_(notes), revision_(revision) {}

    // Views into string literals with static storage duration, so they outlive
    // every caller. A LibraryMaterial is never persisted; what a document
    // persists is the owning MaterialLibraryKey from key().
    std::string_view library_;
    std::string_view entry_;
    std::string_view designation_;
    std::string_view standard_;
    std::string_view family_;
    std::string_view notes_;
    int revision_;
};

/// The name of the library BetterCAD ships: "bettercad".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view builtInLibraryName() noexcept;

/// Every built-in entry, in a fixed order that does not depend on any
/// container's iteration.
///
/// The order is the order the table declares, which is stable across builds,
/// configurations and runs. It is presentation only: an entry's identity is its
/// key, never its position here, so reordering the table cannot change which
/// material anything refers to.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const LibraryMaterial> builtInMaterials() noexcept;

/// The built-in entry with @p entry as its key.
///
/// Fails with NotFound, naming the key, if there is none. It never falls back
/// to a nearby designation or to a generic material: resolving a material by
/// anything but its key is what ADR-025 exists to prevent.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<LibraryMaterial>
findLibraryMaterial(std::string_view library, std::string_view entry);

/// The built-in entry @p key names, requiring the revision to match too.
///
/// Fails with NotFound if the entry is unknown, and with FailedPrecondition,
/// naming both revisions, if the library has moved on. A caller that wants to
/// know whether an imported material is still current asks this; it is never
/// asked while loading a document or while solving.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<LibraryMaterial>
findLibraryMaterial(const MaterialLibraryKey& key);

} // namespace bettercad::materials
