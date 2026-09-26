#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/features/Export.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

// The material document object (P15-MAT-001, ADR-025).
//
// A material is a document object: it has an ObjectId, an object name, a
// revision, undo/redo, a place in the dependency graph and a file
// representation, all from machinery that already exists. It lives in features
// because that is where document objects describing a part already live
// (ADR-028).
//
// This milestone is identity and metadata. Property VALUES -- density, modulus,
// conductivity -- are later milestones, and a material carrying none is not a
// broken material: ADR-027 makes Unknown a legitimate state.
namespace bettercad::features {

/// What a material is, apart from its identity and its object name.
///
/// Every field here is descriptive. None of them is identity, and changing any
/// of them leaves the MaterialId untouched.
struct MaterialDefinition {
    /// The engineering label, e.g. "Aluminium 6061-T6".
    ///
    /// Free text, and deliberately not an identifier: the object name already
    /// carries the identifier rule, and an engineering label needs spaces,
    /// hyphens and digits that a name cannot have. Two materials may carry the
    /// same designation. Empty means none was given.
    std::string designation;
    /// The standard the designation is taken from, e.g. "ASTM B221". Empty
    /// means none is cited, which is a real answer and not a gap to fill.
    std::string standard;
    /// A grouping label, e.g. "Aluminium Alloy".
    ///
    /// Free text rather than an enumeration or a category ID. No ADR fixes a
    /// material taxonomy, and an enumeration would have to be extended -- and
    /// its file representation migrated -- every time a user has a material
    /// BetterCAD's authors did not think of. Any family is therefore accepted,
    /// including one no built-in entry uses.
    std::string family;
    /// Anything worth recording about this material that the fields above do
    /// not carry.
    std::string notes;
    /// Where this material was imported from, if it was.
    ///
    /// This is provenance (ADR-028), not a live reference: the document never
    /// consults the library through it. It is what lets a later milestone say
    /// "this came from bettercad/al-6061-t6 rev 1 and the library now has rev 2"
    /// and leave the decision to the user. std::nullopt for a material the user
    /// created.
    std::optional<materials::MaterialLibraryKey> origin;

    friend bool operator==(const MaterialDefinition&, const MaterialDefinition&) = default;
};

/// Whether @p definition can be stored.
///
/// Every field is optional free text, so this accepts a great deal, including a
/// wholly empty definition: a material the user has named and not yet described
/// is a normal intermediate state. What it does reject is an origin that is not
/// a usable library key, because a half-filled key cannot be resolved later and
/// would make provenance a lie.
///
/// There is no normalisation. Text is stored as given -- not trimmed, not
/// case-folded, not Unicode-normalised -- following the title block, which is
/// the repository's existing convention for descriptive text. Case-folding in
/// particular would be a way for "Steel" and "steel" to become one thing, and
/// nothing in this module compares designations case-insensitively.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void>
validateMaterialDefinition(const MaterialDefinition& definition);

/// One material owned by a document (type name "material").
class BETTERCAD_FEATURES_EXPORT Material final : public DocumentObject {
public:
    using Definition = MaterialDefinition;
    static constexpr std::string_view kTypeName = "material";

    [[nodiscard]] static Result<std::unique_ptr<Material>> create(std::string name,
                                                                  MaterialDefinition definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    // dependencies() is the base's: a material depends on nothing. Things
    // depend on IT -- an assignment, and through that a mass -- which is the
    // direction that keeps the graph acyclic (ADR-026).

    /// This material's ID, narrowed. Valid once the document owns it.
    [[nodiscard]] MaterialId materialId() const noexcept {
        return MaterialId::fromValue(id().value());
    }

    [[nodiscard]] const MaterialDefinition& definition() const noexcept { return definition_; }

    /// Replaces the definition. Returns whether anything changed, so the
    /// document only bumps the revision on an effective change.
    ///
    /// The ID is not touched, and cannot be: nothing here can reach it.
    Result<bool> setDefinition(MaterialDefinition definition);

private:
    explicit Material(std::string name, MaterialDefinition definition);

    MaterialDefinition definition_;
};

} // namespace bettercad::features
