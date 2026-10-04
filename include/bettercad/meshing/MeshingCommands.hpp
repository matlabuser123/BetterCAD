#pragma once

// Undoable meshing edits (P16-CMD-001).
//
// THE HISTORY HOLDS CANONICAL MESHING INTENT AND NOTHING ELSE. No nodes, no
// elements, no connectivity, no boundary facet identities, no quality report,
// no mapping, no render buffer, no backend handle. Every one of those is
// derived, and recomputes from the intent an undo restores -- which is why
// undoing a size change gives the old mesh's sizing back without the old mesh
// ever having been stored.
//
//     edit mesh size  ->  history stores a Length
//     NOT             ->  history stores 100,000 generated tetrahedra
//
// TWO OF THE COMMANDS ARE THIN WRAPPERS, AND DELIBERATELY SO. A MeshControl IS
// a DocumentObject, so AddObjectCommand and DeleteObjectCommand already create
// and delete one correctly, including restoring the same ObjectId on redo and
// the whole object on undo. That was verified by probe before any of this was
// written -- and the probe found a real defect in the process, because a clone
// must carry its ID or the generic redo cannot re-insert it
// (MeshControlProbe_* in tests/meshing/MeshControlTests.cpp).
//
// EVERY EDIT CARRIES A BEFORE AND AN AFTER OF ONE SMALL VALUE. The alternative
// -- inverting each field on undo -- is a list of things to forget, and it
// grows every time the definition gains a field. A MeshControlDefinition is a
// body handle, a few lengths and angles, a map of thresholds and two short
// vectors of references, so a pair of them is proportional to the number of
// CONTROLS and not to the size of any mesh built from them.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/MeshControl.hpp>

#include <optional>
#include <string>

namespace bettercad::meshing {

/// Creates a meshing control; redo recreates it with the same MeshControlId.
///
/// The ID is part of the engineering intent rather than an implementation
/// detail: a mesh is generated for a named control and a later milestone will
/// persist references to it, so a redo that minted a new one would leave every
/// reference to the original dangling.
class BETTERCAD_MESHING_EXPORT CreateMeshControlCommand final : public Command {
public:
    /// @param name        the object name: a valid identifier, unique in the
    ///                    document. A duplicate is refused rather than
    ///                    adjusted.
    /// @param definition  the canonical intent, validated on execute.
    CreateMeshControlCommand(std::string name, MeshControlDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The created control's ID; invalid before a successful execute().
    [[nodiscard]] MeshControlId meshControlId() const noexcept;

private:
    std::string name_;
    MeshControlDefinition definition_;
    std::optional<AddObjectCommand> add_{};
};

/// Deletes a meshing control; undo restores it with the same ID and name.
class BETTERCAD_MESHING_EXPORT DeleteMeshControlCommand final : public Command {
public:
    explicit DeleteMeshControlCommand(MeshControlId control) noexcept;

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    MeshControlId control_;
    std::optional<DeleteObjectCommand> remove_{};
};

/// The shape every edit of one control's definition takes.
///
/// WHY A BASE AND NOT SEVEN COPIES. Each edit differs only in how it derives
/// the new definition from the old one and in what it is called; capturing the
/// before-state, applying the after-state, undoing exactly and redoing exactly
/// are the same four lines every time, and the same four lines are where an
/// atomicity mistake would hide. Written once, they are testable once.
///
/// FAILURE IS ATOMIC BY CONSTRUCTION. `apply` goes through
/// `Document::modifyObject`, which validates through `MeshControl::setDefinition`
/// before assigning anything, and the before-state is recorded only after a
/// successful apply -- so a refused edit leaves the document untouched and the
/// command unexecuted, and `CommandHistory` records only a command whose
/// execute() succeeded.
class BETTERCAD_MESHING_EXPORT MeshControlEditCommand : public Command {
public:
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// Whether the edit changed anything. Valid after a successful execute().
    ///
    /// A no-op edit SUCCEEDS and changes nothing: no revision bump, and so no
    /// mesh invalidation. It is still recorded by `CommandHistory`, which
    /// records every command whose execute() succeeded -- the same behaviour
    /// P15's material edit has, and not something to special-case in one
    /// module.
    [[nodiscard]] bool changedAnything() const noexcept { return changed_; }

protected:
    explicit MeshControlEditCommand(MeshControlId control) noexcept : control_(control) {}

    /// The new definition, derived from @p before, or a refusal.
    ///
    /// Refusals here are the command's own preconditions -- "that face already
    /// has a control", "there is no set with that identity". Whether the
    /// RESULT is valid is not checked here: `MeshControl::setDefinition`
    /// delegates that to the validators that own the rules.
    [[nodiscard]] virtual Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const = 0;

    MeshControlId control_;

private:
    std::optional<MeshControlDefinition> before_{};
    std::optional<MeshControlDefinition> after_{};
    bool changed_ = false;
};

/// Replaces a control's whole definition.
///
/// The settings-edit command: one user-visible action, one undo step, with the
/// before and after of one value. Used where a dialog edits several fields at
/// once, which the framework has no compound-command type for.
class BETTERCAD_MESHING_EXPORT SetMeshControlDefinitionCommand final
    : public MeshControlEditCommand {
public:
    SetMeshControlDefinitionCommand(MeshControlId control, MeshControlDefinition definition);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    MeshControlDefinition definition_;
};

/// Sets the global element-size target, or clears it back to the default.
class BETTERCAD_MESHING_EXPORT SetGlobalMeshSizeCommand final : public MeshControlEditCommand {
public:
    /// @param target  the upper bound on element size, or nullopt for
    ///                BetterCAD's scale-relative default.
    SetGlobalMeshSizeCommand(MeshControlId control, std::optional<Length> target) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    std::optional<Length> target_;
};

/// Adds a local sizing control for one CAD face.
///
/// A LOCAL CONTROL'S IDENTITY IS ITS FaceName, which is P16-SIZE-001's
/// contract and not a choice made here: it refuses two controls on one face,
/// and the order of the stored vector carries no meaning. So there is no ID to
/// mint, nothing for a redo to allocate differently, and no way for an undone
/// add to collide with a later one -- the three hazards a synthetic
/// local-control ID would have introduced.
class BETTERCAD_MESHING_EXPORT AddLocalMeshSizingCommand final : public MeshControlEditCommand {
public:
    AddLocalMeshSizingCommand(MeshControlId control, FaceName face, Length targetSize);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    FaceName face_;
    Length targetSize_;
};

/// Changes the target size of the local control on one face.
class BETTERCAD_MESHING_EXPORT EditLocalMeshSizingCommand final : public MeshControlEditCommand {
public:
    EditLocalMeshSizingCommand(MeshControlId control, FaceName face, Length targetSize);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    FaceName face_;
    Length targetSize_;
};

/// Removes the local control on one face.
///
/// Undo restores the same face reference and the same size, because the
/// before-state is the whole definition. There is no identity to regenerate.
class BETTERCAD_MESHING_EXPORT RemoveLocalMeshSizingCommand final : public MeshControlEditCommand {
public:
    RemoveLocalMeshSizingCommand(MeshControlId control, FaceName face);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    FaceName face_;
};

/// Adds a named boundary set.
///
/// The set holds `FaceName`s -- the intent -- and never the facets they
/// currently resolve to: a `BoundaryFacetSet` is derived from one mesh
/// generation and would be wrong at the next remesh.
class BETTERCAD_MESHING_EXPORT AddBoundarySetCommand final : public MeshControlEditCommand {
public:
    AddBoundarySetCommand(MeshControlId control, NamedBoundarySet set);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    NamedBoundarySet set_;
};

/// Replaces a named boundary set's name and face selection, keeping its ID.
class BETTERCAD_MESHING_EXPORT EditBoundarySetCommand final : public MeshControlEditCommand {
public:
    EditBoundarySetCommand(MeshControlId control, NamedBoundarySet set);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    NamedBoundarySet set_;
};

/// Removes a named boundary set; undo restores it with the same identity.
class BETTERCAD_MESHING_EXPORT RemoveBoundarySetCommand final : public MeshControlEditCommand {
public:
    RemoveBoundarySetCommand(MeshControlId control, BoundarySetId set) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<MeshControlDefinition> edit(
        const MeshControlDefinition& before) const override;

private:
    BoundarySetId set_;
};

/// The control with @p id, or nullptr when the document has no such control.
///
/// Refuses an ID that names an object of another kind, rather than returning
/// it as a control: a sketch's ID handed to a meshing command must not
/// silently work.
[[nodiscard]] BETTERCAD_MESHING_EXPORT const MeshControl* findMeshControl(
    const Document& document, MeshControlId id) noexcept;

/// Every meshing control in the document, in ascending ID order.
[[nodiscard]] BETTERCAD_MESHING_EXPORT std::vector<MeshControlId> meshControls(
    const Document& document);

} // namespace bettercad::meshing
