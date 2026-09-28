#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Material.hpp>

#include <memory>
#include <optional>
#include <string>

// Undoable material edits (P15-CMD-001).
//
// THE HISTORY HOLDS CANONICAL ENGINEERING INTENT AND NOTHING ELSE. No mass, no
// volume, no centre of mass, no inertia, no derived shear or bulk modulus, no
// completeness report, no effective-material result. Every one of those is derived
// (ADR-026, ADR-027, ADR-028) and recomputes from the intent an undo restores --
// which is why undoing a density edit gives the old mass back without the old mass
// ever having been stored.
//
// TWO OF THE FIVE COMMANDS ARE THIN WRAPPERS, AND DELIBERATELY SO. A material IS a
// DocumentObject, so AddObjectCommand and DeleteObjectCommand already create and
// delete one correctly -- including restoring the same ObjectId on redo, restoring
// the whole object on undo, and putting back what the configurations said about it.
// That was verified by probe before any of this was written, not assumed.
//
// So CreateMaterialCommand and DeleteMaterialCommand wrap them rather than
// reimplementing them, exactly as CreateFeatureCommand<F> wraps AddObjectCommand.
// What the wrappers add is a typed ID at the call site, a material-shaped
// description, and a precondition that the ID really names a material -- so a
// sketch's ID cannot be passed to a material delete and silently work.
//
// The three that are genuinely new are the ones nothing generic covers: replacing a
// material's definition, and setting or clearing the document's material
// assignment, which is document-level state rather than an object.
namespace bettercad::features {

/// Creates a material; redo recreates it with the same MaterialId.
///
/// The ID is part of the engineering intent, not an implementation detail: an
/// assignment names a material by ID (ADR-025, P15-ASSIGN-001), so a redo that
/// minted a new one would leave every reference to the original dangling. The
/// underlying AddObjectCommand restores the ID, and a test pins it.
class BETTERCAD_FEATURES_EXPORT CreateMaterialCommand final : public Command {
public:
    /// @param name        the object name, which must be a valid identifier and
    ///                    available in the document -- names are unique, and a
    ///                    duplicate is refused rather than adjusted
    ///                    (P15-CUSTOM-001).
    /// @param definition  the canonical material state, validated on execute.
    CreateMaterialCommand(std::string name, MaterialDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The created material's ID; invalid before a successful execute().
    [[nodiscard]] MaterialId materialId() const noexcept;

private:
    std::string name_;
    MaterialDefinition definition_;
    std::optional<AddObjectCommand> add_;
};

/// Deletes a material; undo restores it with the same MaterialId and the same
/// canonical state.
///
/// The deletion POLICY is the qualified one and is not restated here: removing a
/// material that something is assigned to is allowed, and the assignment becomes
/// Unresolved rather than silently rebinding (P15-ASSIGN-001). Undo restores the
/// material under its original ID, so the assignment resolves again -- and because
/// the assignment holds an ID rather than a name, it can only ever resolve back to
/// the material that went away, never to a same-named replacement.
class BETTERCAD_FEATURES_EXPORT DeleteMaterialCommand final : public Command {
public:
    explicit DeleteMaterialCommand(MaterialId id) noexcept;

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    MaterialId id_;
    std::optional<DeleteObjectCommand> remove_;
};

/// Replaces a material's whole canonical definition; undo restores exactly what was
/// there before.
///
/// WHOLE-DEFINITION, and that is what makes the awkward cases correct rather than
/// nearly correct. The before and after states are complete MaterialDefinitions, so
/// a property going Known -> Unknown comes back Known and not zero; a provenance
/// record and the value it describes move together and cannot be undone apart; and
/// an edit that touches metadata and properties at once reverses as one step.
///
/// It stores no derived value. Editing E and nu changes only those, and the shear
/// and bulk moduli recompute from whatever is restored (ADR-027) -- there is no slot
/// for them to be stored in, so they could not enter the history even by mistake.
///
/// The ID is never part of what is replaced, so editing content cannot allocate a
/// new material and cannot disturb an assignment.
///
/// Validation is the qualified one: the definition is checked before the document is
/// touched, so a rejected edit leaves the material exactly as it was and the command
/// never reaches the history.
class BETTERCAD_FEATURES_EXPORT EditMaterialCommand final : public Command {
public:
    EditMaterialCommand(MaterialId id, MaterialDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    Result<void> apply(Document& document, const MaterialDefinition& definition);

    MaterialId id_;
    MaterialDefinition after_;
    std::optional<MaterialDefinition> before_;
};

/// Assigns a material to the document; undo restores whatever was assigned before,
/// including nothing.
///
/// DIRECT INTENT ONLY. The canonical state is one optional MaterialId on the
/// Document (ADR-026, P15-ASSIGN-001) -- there is no per-body, per-occurrence or
/// per-configuration assignment to confuse it with, and no effective-material result
/// is stored. So this command replaces one ID with another and nothing else, and an
/// undo cannot reach a different object's intent.
///
/// The material must exist in THIS document; assigning one that does not is refused,
/// so Unresolved stays a state the world produces rather than one the API does.
class BETTERCAD_FEATURES_EXPORT AssignMaterialCommand final : public Command {
public:
    explicit AssignMaterialCommand(MaterialId id) noexcept;

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    MaterialId id_;
    /// The assignment before this command ran. The outer optional is the
    /// "has executed" flag; the inner one is the assignment itself, which may
    /// legitimately have been absent.
    std::optional<std::optional<MaterialId>> before_;
};

/// Clears the document's material assignment; undo restores what it was.
///
/// Clearing an already-clear assignment succeeds and changes nothing, which follows
/// SetActiveConfigurationCommand rather than inventing a convention: the command
/// records the previous state and the document's own revision only moves on a real
/// change. It removes the DIRECT assignment and can do nothing else -- there is no
/// inherited assignment above it to reach.
class BETTERCAD_FEATURES_EXPORT RemoveMaterialAssignmentCommand final : public Command {
public:
    RemoveMaterialAssignmentCommand() noexcept = default;

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    std::optional<std::optional<MaterialId>> before_;
};

} // namespace bettercad::features
