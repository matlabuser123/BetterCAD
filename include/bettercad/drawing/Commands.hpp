#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/Configurations.hpp>
#include <bettercad/drawing/Annotation.hpp>
#include <bettercad/drawing/Dimension.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/Sheet.hpp>
#include <bettercad/drawing/View.hpp>

#include <memory>
#include <optional>
#include <string>

// Drawing edits as commands (P14-CMD-001).
//
// ONE command system and ONE history: these are commands in the system
// `core/document/Command.hpp` defines, exactly as the assembly commands are.
// Nothing here is a second mechanism, and nothing here is a transaction of its
// own -- `CommandHistory` already gives the transaction: a command that fails
// is discarded and never reaches the undo stack.
//
// WHY THESE EXIST AT ALL, given core's generic AddObjectCommand and
// DeleteObjectCommand already add and remove any DocumentObject with the
// same-ID guarantee: what the generic pair cannot do is VALIDATE. createView()
// checks the sheet exists, the kind's own fields are present and no other
// kind's are, the parent is on the same sheet, and the chain reaches a base
// view. **removeView() refuses while another view is projected from it** --
// and DeleteObjectCommand, which calls Document::removeObject() directly,
// walks straight past that and orphans the child. So each command here wraps
// the module function that holds the policy, rather than replacing it: two
// ways to delete a view, one checked and one not, ends with the wrong one
// being used.
//
// WHAT IS UNDO PAYLOAD, AND WHAT IS NEVER. A command stores canonical intent
// only -- a definition before, a definition after, or the removed object
// itself. It never stores a projection, a measured value, a BOM row or an item
// number, because none of those is intent: they are recomputed from the
// document on every call (ADR-011, ADR-014) and a stored one would be a
// remembered answer to a question whose answer has changed. Undoing to a
// placement means restoring the placement, never the picture it produced.
//
// A MOVE TAKES AN ABSOLUTE POSITION, never a delta, and undo restores the
// position that was there rather than subtracting what was added. A hundred
// move/undo/redo cycles therefore land on exactly the number they started on;
// a delta-based undo would accumulate floating-point drift.
namespace bettercad::drawing {

// --- Sheets -------------------------------------------------------------------------------------

/// Creates a sheet, validated as createSheet() does.
///
/// Redo restores the same SheetId, so a view that named it before an undo
/// still names it after the redo.
class BETTERCAD_DRAWING_EXPORT CreateSheetCommand final : public Command {
public:
    CreateSheetCommand(std::string name, SheetDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The sheet created; invalid before execute().
    [[nodiscard]] SheetId sheetId() const noexcept { return id_; }

private:
    std::string name_;
    SheetDefinition definition_;
    SheetId id_{};
    /// Held between undo and redo so redo restores the SAME object, with the
    /// ID anything referring to it still names.
    std::unique_ptr<DocumentObject> kept_{};
};

/// Replaces a sheet's definition: its format, orientation, margins, scale,
/// projection convention or title block. Validated as setSheetDefinition()
/// does, so a size that leaves no usable region is refused and nothing moves.
class BETTERCAD_DRAWING_EXPORT SetSheetDefinitionCommand final : public Command {
public:
    SetSheetDefinitionCommand(SheetId sheet, SheetDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    SheetId sheet_;
    SheetDefinition definition_;
    std::optional<SheetDefinition> before_{};
};

/// Deletes a sheet; undo restores it with the same ID, name and configuration
/// overrides.
class BETTERCAD_DRAWING_EXPORT DeleteSheetCommand final : public Command {
public:
    explicit DeleteSheetCommand(SheetId sheet) noexcept : sheet_(sheet) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    SheetId sheet_;
    std::string name_{};
    std::unique_ptr<DocumentObject> object_{};
    /// What the configurations said about this object before it went; the
    /// removal clears it and undo must put it back, as core's
    /// DeleteObjectCommand does.
    ObjectOverrides overrides_{};
};

// --- Views --------------------------------------------------------------------------------------

/// Creates a view, validated as createView() does.
class BETTERCAD_DRAWING_EXPORT CreateViewCommand final : public Command {
public:
    CreateViewCommand(std::string name, ViewDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The view created; invalid before execute().
    [[nodiscard]] ViewId viewId() const noexcept { return id_; }

private:
    std::string name_;
    ViewDefinition definition_;
    ViewId id_{};
    std::unique_ptr<DocumentObject> kept_{};
};

/// Replaces a view's definition: its scale, orientation, hidden-line
/// settings, section plane, hatch or spacing. Validated as setViewDefinition()
/// does.
class BETTERCAD_DRAWING_EXPORT SetViewDefinitionCommand final : public Command {
public:
    SetViewDefinitionCommand(ViewId view, ViewDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ViewId view_;
    ViewDefinition definition_;
    std::optional<ViewDefinition> before_{};
};

/// Moves a view to @p placement on its sheet, in sheet millimetres.
///
/// It changes the placement and nothing else the view says about itself. A
/// projected, section or auxiliary view derives its placement from its parent
/// and stores none (ADR-018), so this is REFUSED for one, in the validator's
/// own words -- move one of those by its `spacing` through
/// SetViewDefinitionCommand.
class BETTERCAD_DRAWING_EXPORT MoveViewCommand final : public Command {
public:
    MoveViewCommand(ViewId view, Point2D placement) noexcept : view_(view), placement_(placement) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ViewId view_;
    Point2D placement_;
    std::optional<Point2D> before_{};
};

/// Deletes a view, refusing while another view is projected from it exactly as
/// removeView() does; undo restores it with the same ID.
class BETTERCAD_DRAWING_EXPORT DeleteViewCommand final : public Command {
public:
    explicit DeleteViewCommand(ViewId view) noexcept : view_(view) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ViewId view_;
    std::string name_{};
    std::unique_ptr<DocumentObject> object_{};
    ObjectOverrides overrides_{};
};

// --- Dimensions ---------------------------------------------------------------------------------

/// Creates a dimension, validated as createDimension() does.
///
/// What is stored is the REFERENCE INTENT -- the targets, the type, the
/// formatting, the placement. The measured value is not command state: it is
/// resolved from the model every time it is asked for, so a redo after the
/// model changed shows the new number and not the one that was on screen when
/// the command first ran.
class BETTERCAD_DRAWING_EXPORT CreateDimensionCommand final : public Command {
public:
    CreateDimensionCommand(std::string name, DimensionDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The dimension created; invalid before execute().
    [[nodiscard]] DimensionId dimensionId() const noexcept { return id_; }

private:
    std::string name_;
    DimensionDefinition definition_;
    DimensionId id_{};
    std::unique_ptr<DocumentObject> kept_{};
};

/// Replaces a dimension's definition: its targets, type, formatting or
/// tolerance. Validated as setDimensionDefinition() does.
class BETTERCAD_DRAWING_EXPORT SetDimensionDefinitionCommand final : public Command {
public:
    SetDimensionDefinitionCommand(DimensionId dimension, DimensionDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    DimensionId dimension_;
    DimensionDefinition definition_;
    std::optional<DimensionDefinition> before_{};
};

/// Moves a dimension's text to @p placement, changing nothing it measures.
class BETTERCAD_DRAWING_EXPORT MoveDimensionCommand final : public Command {
public:
    MoveDimensionCommand(DimensionId dimension, Point2D placement) noexcept
        : dimension_(dimension), placement_(placement) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    DimensionId dimension_;
    Point2D placement_;
    std::optional<Point2D> before_{};
};

/// Deletes a dimension; undo restores it with the same ID.
class BETTERCAD_DRAWING_EXPORT DeleteDimensionCommand final : public Command {
public:
    explicit DeleteDimensionCommand(DimensionId dimension) noexcept : dimension_(dimension) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    DimensionId dimension_;
    std::string name_{};
    std::unique_ptr<DocumentObject> object_{};
    ObjectOverrides overrides_{};
};

// --- Annotations, which is also where a BOM table and a balloon live --------------------------
//
// A BOM table and a balloon are ANNOTATION KINDS, not object kinds of their
// own (ADR-022), so these four commands are the BOM and balloon commands too.
// There is deliberately no command for a row, a quantity or an item number:
// none of them is stored anywhere, all three are computed from the active
// occurrence set on every call, and a command that "edited" one would be
// writing down an answer the assembly is entitled to change.
//
// A balloon's canonical target is the OCCURRENCE (a ComponentId), never the
// item number it happens to display. Retargeting one is an explicit edit
// through SetAnnotationDefinitionCommand; nothing here rebinds a reference on
// its own.

/// Creates an annotation -- a note, leader, centreline, centre mark, hole
/// callout, surface-finish symbol, datum symbol, feature-control frame, BOM
/// table or balloon. Validated as createAnnotation() does.
class BETTERCAD_DRAWING_EXPORT CreateAnnotationCommand final : public Command {
public:
    CreateAnnotationCommand(std::string name, AnnotationDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The annotation created; invalid before execute().
    [[nodiscard]] AnnotationId annotationId() const noexcept { return id_; }

private:
    std::string name_;
    AnnotationDefinition definition_;
    AnnotationId id_{};
    std::unique_ptr<DocumentObject> kept_{};
};

/// Replaces an annotation's definition: its text, style, target or type.
/// Validated as setAnnotationDefinition() does.
class BETTERCAD_DRAWING_EXPORT SetAnnotationDefinitionCommand final : public Command {
public:
    SetAnnotationDefinitionCommand(AnnotationId annotation, AnnotationDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    AnnotationId annotation_;
    AnnotationDefinition definition_;
    std::optional<AnnotationDefinition> before_{};
};

/// Moves an annotation to @p placement, which is also how a BOM table and a
/// balloon are moved.
class BETTERCAD_DRAWING_EXPORT MoveAnnotationCommand final : public Command {
public:
    MoveAnnotationCommand(AnnotationId annotation, Point2D placement) noexcept
        : annotation_(annotation), placement_(placement) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    AnnotationId annotation_;
    Point2D placement_;
    std::optional<Point2D> before_{};
};

/// Deletes an annotation; undo restores it with the same ID.
class BETTERCAD_DRAWING_EXPORT DeleteAnnotationCommand final : public Command {
public:
    explicit DeleteAnnotationCommand(AnnotationId annotation) noexcept : annotation_(annotation) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    AnnotationId annotation_;
    std::string name_{};
    std::unique_ptr<DocumentObject> object_{};
    ObjectOverrides overrides_{};
};

} // namespace bettercad::drawing
