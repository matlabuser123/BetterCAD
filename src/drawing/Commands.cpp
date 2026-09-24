#include <bettercad/drawing/Commands.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>

#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

[[nodiscard]] std::unexpected<Error> notExecuted(std::string_view what) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("cannot {} a command that has not been executed", what));
}

// --- The four shapes, written once ---------------------------------------------------------
//
// File-local templates, so the fifteen commands cannot drift apart in the
// details that matter -- what is captured, in what order, and what is left
// alone on failure. They are deliberately NOT in the header: a class template
// member is implicitly inline, and an inline member carrying the export macro
// becomes dllimport in debug-shared, which is the defect that preset caught
// twice in P14 already.

/// Undo of a creation: take the object out and keep it, so redo can put the
/// SAME one back. Restoring by ID is what lets a dimension that named a view
/// before an undo still name it after the redo.
template <typename Id>
[[nodiscard]] Result<std::unique_ptr<DocumentObject>> takeBack(Document& document, Id id) {
    auto removed = document.removeObject(ObjectId{id});
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return std::move(*removed);
}

/// Redo of a creation: put the kept object back under its original ID.
[[nodiscard]] Result<void> putBack(Document& document, std::unique_ptr<DocumentObject>& kept) {
    if (kept == nullptr) {
        return notExecuted("redo");
    }
    if (auto inserted = document.insertObject(kept->clone()); !inserted) {
        return inserted;
    }
    kept.reset();
    return {};
}

/// execute() of a definition edit: read what is there, write the new one
/// through the module's validated setter, and only then record what was
/// there. Recording last is what makes a REFUSED edit leave the command
/// unexecuted as well as the document unchanged.
template <typename Object, typename Id, typename Definition, typename Find, typename Set>
[[nodiscard]] Result<void> applyDefinition(Document& document, Id id, const Definition& definition,
                                           std::optional<Definition>& before, Find find, Set set,
                                           std::string_view kind) {
    const Object* object = find(document, id);
    if (object == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a {} of this document", id, kind));
    }
    Definition previous = object->definition();
    if (auto changed = set(document, id, definition); !changed) {
        return std::unexpected(changed.error());
    }
    before = std::move(previous);
    return {};
}

/// undo()/redo() of a definition edit: write the recorded definition back.
template <typename Id, typename Definition, typename Set>
[[nodiscard]] Result<void> restoreDefinition(Document& document, Id id,
                                             const Definition& definition, Set set) {
    if (auto changed = set(document, id, definition); !changed) {
        return std::unexpected(changed.error());
    }
    return {};
}

/// execute() of a move: the placement changes and nothing else the object
/// says about itself does.
template <typename Object, typename Id, typename Find, typename Set>
[[nodiscard]] Result<void> applyPlacement(Document& document, Id id, Point2D placement,
                                          std::optional<Point2D>& before, Find find, Set set,
                                          std::string_view kind) {
    const Object* object = find(document, id);
    if (object == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a {} of this document", id, kind));
    }
    auto definition = object->definition();
    const Point2D previous = definition.placement;
    definition.placement = placement;
    if (auto changed = set(document, id, definition); !changed) {
        return std::unexpected(changed.error());
    }
    before = previous;
    return {};
}

/// execute() of a deletion: capture the overrides BEFORE the removal, which
/// is what clears them, then take the object out and keep it.
template <typename Id>
[[nodiscard]] Result<void> applyDeletion(Document& document, Id id, std::string& name,
                                         std::unique_ptr<DocumentObject>& object,
                                         ObjectOverrides& overrides) {
    overrides = document.configurationOverridesFor(ObjectId{id});
    auto removed = takeBack(document, id);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    name = (*removed)->name();
    object = std::move(*removed);
    return {};
}

/// undo() of a deletion: the object first, then what the configurations said
/// about it -- an override may only name an object the document holds.
[[nodiscard]] Result<void> restoreDeletion(Document& document, ObjectId id,
                                           std::unique_ptr<DocumentObject>& object,
                                           const ObjectOverrides& overrides) {
    if (object == nullptr) {
        return notExecuted("undo");
    }
    if (auto inserted = document.insertObject(object->clone()); !inserted) {
        return inserted;
    }
    object.reset();
    document.restoreConfigurationOverrides(id, overrides);
    return {};
}

// The module functions as callables, so the templates above can take them.
const auto findSheetIn = [](const Document& d, SheetId id) { return findSheet(d, id); };
const auto setSheetIn = [](Document& d, SheetId id, const SheetDefinition& x) {
    return setSheetDefinition(d, id, x);
};
const auto findViewIn = [](const Document& d, ViewId id) { return findView(d, id); };
const auto setViewIn = [](Document& d, ViewId id, const ViewDefinition& x) {
    return setViewDefinition(d, id, x);
};
const auto findDimensionIn = [](const Document& d, DimensionId id) { return findDimension(d, id); };
const auto setDimensionIn = [](Document& d, DimensionId id, const DimensionDefinition& x) {
    return setDimensionDefinition(d, id, x);
};
const auto findAnnotationIn = [](const Document& d, AnnotationId id) {
    return findAnnotation(d, id);
};
const auto setAnnotationIn = [](Document& d, AnnotationId id, const AnnotationDefinition& x) {
    return setAnnotationDefinition(d, id, x);
};

} // namespace

// --- CreateSheetCommand -------------------------------------------------------------------------

CreateSheetCommand::CreateSheetCommand(std::string name, SheetDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateSheetCommand::description() const {
    return std::format("Create sheet '{}'", name_);
}

Result<void> CreateSheetCommand::execute(Document& document) {
    auto id = createSheet(document, name_, definition_);
    if (!id) {
        return std::unexpected(id.error());
    }
    id_ = *id;
    return {};
}

Result<void> CreateSheetCommand::undo(Document& document) {
    if (!id_.isValid()) {
        return notExecuted("undo");
    }
    auto removed = takeBack(document, id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    kept_ = std::move(*removed);
    return {};
}

Result<void> CreateSheetCommand::redo(Document& document) {
    return putBack(document, kept_);
}

// --- SetSheetDefinitionCommand ------------------------------------------------------------------

SetSheetDefinitionCommand::SetSheetDefinitionCommand(SheetId sheet, SheetDefinition definition)
    : sheet_(sheet), definition_(std::move(definition)) {}

std::string SetSheetDefinitionCommand::description() const {
    return std::format("Edit {}", sheet_);
}

Result<void> SetSheetDefinitionCommand::execute(Document& document) {
    return applyDefinition<Sheet>(document, sheet_, definition_, before_, findSheetIn, setSheetIn,
                                  "sheet");
}

Result<void> SetSheetDefinitionCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    return restoreDefinition(document, sheet_, *before_, setSheetIn);
}

Result<void> SetSheetDefinitionCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    return restoreDefinition(document, sheet_, definition_, setSheetIn);
}

// --- DeleteSheetCommand -------------------------------------------------------------------------

std::string DeleteSheetCommand::description() const {
    return name_.empty() ? std::format("Delete {}", sheet_)
                         : std::format("Delete sheet '{}'", name_);
}

Result<void> DeleteSheetCommand::execute(Document& document) {
    if (findSheet(document, sheet_) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no sheet {}", sheet_));
    }
    return applyDeletion(document, sheet_, name_, object_, overrides_);
}

Result<void> DeleteSheetCommand::undo(Document& document) {
    return restoreDeletion(document, ObjectId{sheet_}, object_, overrides_);
}

Result<void> DeleteSheetCommand::redo(Document& document) {
    if (name_.empty()) {
        return notExecuted("redo");
    }
    return execute(document);
}

// --- CreateViewCommand --------------------------------------------------------------------------

CreateViewCommand::CreateViewCommand(std::string name, ViewDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateViewCommand::description() const {
    return std::format("Create view '{}'", name_);
}

Result<void> CreateViewCommand::execute(Document& document) {
    auto id = createView(document, name_, definition_);
    if (!id) {
        return std::unexpected(id.error());
    }
    id_ = *id;
    return {};
}

Result<void> CreateViewCommand::undo(Document& document) {
    if (!id_.isValid()) {
        return notExecuted("undo");
    }
    // The same refusal a direct removal gets: a view with a child projected
    // from it cannot go, and that is as true when the going is an undo.
    if (auto allowed = checkRemoveView(document, id_); !allowed) {
        return allowed;
    }
    auto removed = takeBack(document, id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    kept_ = std::move(*removed);
    return {};
}

Result<void> CreateViewCommand::redo(Document& document) {
    return putBack(document, kept_);
}

// --- SetViewDefinitionCommand -------------------------------------------------------------------

SetViewDefinitionCommand::SetViewDefinitionCommand(ViewId view, ViewDefinition definition)
    : view_(view), definition_(std::move(definition)) {}

std::string SetViewDefinitionCommand::description() const {
    return std::format("Edit {}", view_);
}

Result<void> SetViewDefinitionCommand::execute(Document& document) {
    return applyDefinition<View>(document, view_, definition_, before_, findViewIn, setViewIn,
                                 "view");
}

Result<void> SetViewDefinitionCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    return restoreDefinition(document, view_, *before_, setViewIn);
}

Result<void> SetViewDefinitionCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    return restoreDefinition(document, view_, definition_, setViewIn);
}

// --- MoveViewCommand ----------------------------------------------------------------------------

std::string MoveViewCommand::description() const {
    return std::format("Move {}", view_);
}

Result<void> MoveViewCommand::execute(Document& document) {
    // A projected, section or auxiliary view stores no placement, and the
    // refusal comes from validate() rather than from a second opinion here:
    // "a projected view's placement is derived from its parent's; it stores
    // none".
    return applyPlacement<View>(document, view_, placement_, before_, findViewIn, setViewIn, "view");
}

Result<void> MoveViewCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    // The position that WAS there, not the one that is there minus a delta:
    // restoring the number is what makes a hundred cycles land where they
    // started.
    std::optional<Point2D> ignored;
    auto moved = applyPlacement<View>(document, view_, *before_, ignored, findViewIn, setViewIn,
                                      "view");
    if (!moved) {
        return moved;
    }
    return {};
}

Result<void> MoveViewCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    std::optional<Point2D> ignored;
    return applyPlacement<View>(document, view_, placement_, ignored, findViewIn, setViewIn, "view");
}

// --- DeleteViewCommand --------------------------------------------------------------------------

std::string DeleteViewCommand::description() const {
    return name_.empty() ? std::format("Delete {}", view_) : std::format("Delete view '{}'", name_);
}

Result<void> DeleteViewCommand::execute(Document& document) {
    // removeView()'s own precondition, asked separately because the object
    // has to be kept for undo and removeView() discards it. One rule, one
    // implementation (checkRemoveView).
    if (auto allowed = checkRemoveView(document, view_); !allowed) {
        return allowed;
    }
    return applyDeletion(document, view_, name_, object_, overrides_);
}

Result<void> DeleteViewCommand::undo(Document& document) {
    return restoreDeletion(document, ObjectId{view_}, object_, overrides_);
}

Result<void> DeleteViewCommand::redo(Document& document) {
    if (name_.empty()) {
        return notExecuted("redo");
    }
    return execute(document);
}

// --- CreateDimensionCommand ---------------------------------------------------------------------

CreateDimensionCommand::CreateDimensionCommand(std::string name, DimensionDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateDimensionCommand::description() const {
    return std::format("Create dimension '{}'", name_);
}

Result<void> CreateDimensionCommand::execute(Document& document) {
    auto id = createDimension(document, name_, definition_);
    if (!id) {
        return std::unexpected(id.error());
    }
    id_ = *id;
    return {};
}

Result<void> CreateDimensionCommand::undo(Document& document) {
    if (!id_.isValid()) {
        return notExecuted("undo");
    }
    auto removed = takeBack(document, id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    kept_ = std::move(*removed);
    return {};
}

Result<void> CreateDimensionCommand::redo(Document& document) {
    return putBack(document, kept_);
}

// --- SetDimensionDefinitionCommand --------------------------------------------------------------

SetDimensionDefinitionCommand::SetDimensionDefinitionCommand(DimensionId dimension,
                                                             DimensionDefinition definition)
    : dimension_(dimension), definition_(std::move(definition)) {}

std::string SetDimensionDefinitionCommand::description() const {
    return std::format("Edit {}", dimension_);
}

Result<void> SetDimensionDefinitionCommand::execute(Document& document) {
    return applyDefinition<Dimension>(document, dimension_, definition_, before_, findDimensionIn,
                                      setDimensionIn, "dimension");
}

Result<void> SetDimensionDefinitionCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    return restoreDefinition(document, dimension_, *before_, setDimensionIn);
}

Result<void> SetDimensionDefinitionCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    return restoreDefinition(document, dimension_, definition_, setDimensionIn);
}

// --- MoveDimensionCommand -----------------------------------------------------------------------

std::string MoveDimensionCommand::description() const {
    return std::format("Move {}", dimension_);
}

Result<void> MoveDimensionCommand::execute(Document& document) {
    return applyPlacement<Dimension>(document, dimension_, placement_, before_, findDimensionIn,
                                     setDimensionIn, "dimension");
}

Result<void> MoveDimensionCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    std::optional<Point2D> ignored;
    return applyPlacement<Dimension>(document, dimension_, *before_, ignored, findDimensionIn,
                                     setDimensionIn, "dimension");
}

Result<void> MoveDimensionCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    std::optional<Point2D> ignored;
    return applyPlacement<Dimension>(document, dimension_, placement_, ignored, findDimensionIn,
                                     setDimensionIn, "dimension");
}

// --- DeleteDimensionCommand ---------------------------------------------------------------------

std::string DeleteDimensionCommand::description() const {
    return name_.empty() ? std::format("Delete {}", dimension_)
                         : std::format("Delete dimension '{}'", name_);
}

Result<void> DeleteDimensionCommand::execute(Document& document) {
    if (findDimension(document, dimension_) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a dimension of this document", dimension_));
    }
    return applyDeletion(document, dimension_, name_, object_, overrides_);
}

Result<void> DeleteDimensionCommand::undo(Document& document) {
    return restoreDeletion(document, ObjectId{dimension_}, object_, overrides_);
}

Result<void> DeleteDimensionCommand::redo(Document& document) {
    if (name_.empty()) {
        return notExecuted("redo");
    }
    return execute(document);
}

// --- CreateAnnotationCommand --------------------------------------------------------------------

CreateAnnotationCommand::CreateAnnotationCommand(std::string name, AnnotationDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateAnnotationCommand::description() const {
    return std::format("Create annotation '{}'", name_);
}

Result<void> CreateAnnotationCommand::execute(Document& document) {
    auto id = createAnnotation(document, name_, definition_);
    if (!id) {
        return std::unexpected(id.error());
    }
    id_ = *id;
    return {};
}

Result<void> CreateAnnotationCommand::undo(Document& document) {
    if (!id_.isValid()) {
        return notExecuted("undo");
    }
    auto removed = takeBack(document, id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    kept_ = std::move(*removed);
    return {};
}

Result<void> CreateAnnotationCommand::redo(Document& document) {
    return putBack(document, kept_);
}

// --- SetAnnotationDefinitionCommand -------------------------------------------------------------

SetAnnotationDefinitionCommand::SetAnnotationDefinitionCommand(AnnotationId annotation,
                                                               AnnotationDefinition definition)
    : annotation_(annotation), definition_(std::move(definition)) {}

std::string SetAnnotationDefinitionCommand::description() const {
    return std::format("Edit {}", annotation_);
}

Result<void> SetAnnotationDefinitionCommand::execute(Document& document) {
    return applyDefinition<Annotation>(document, annotation_, definition_, before_,
                                       findAnnotationIn, setAnnotationIn, "annotation");
}

Result<void> SetAnnotationDefinitionCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    return restoreDefinition(document, annotation_, *before_, setAnnotationIn);
}

Result<void> SetAnnotationDefinitionCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    return restoreDefinition(document, annotation_, definition_, setAnnotationIn);
}

// --- MoveAnnotationCommand ----------------------------------------------------------------------

std::string MoveAnnotationCommand::description() const {
    return std::format("Move {}", annotation_);
}

Result<void> MoveAnnotationCommand::execute(Document& document) {
    return applyPlacement<Annotation>(document, annotation_, placement_, before_, findAnnotationIn,
                                      setAnnotationIn, "annotation");
}

Result<void> MoveAnnotationCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    std::optional<Point2D> ignored;
    return applyPlacement<Annotation>(document, annotation_, *before_, ignored, findAnnotationIn,
                                      setAnnotationIn, "annotation");
}

Result<void> MoveAnnotationCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    std::optional<Point2D> ignored;
    return applyPlacement<Annotation>(document, annotation_, placement_, ignored, findAnnotationIn,
                                      setAnnotationIn, "annotation");
}

// --- DeleteAnnotationCommand --------------------------------------------------------------------

std::string DeleteAnnotationCommand::description() const {
    return name_.empty() ? std::format("Delete {}", annotation_)
                         : std::format("Delete annotation '{}'", name_);
}

Result<void> DeleteAnnotationCommand::execute(Document& document) {
    if (findAnnotation(document, annotation_) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not an annotation of this document", annotation_));
    }
    return applyDeletion(document, annotation_, name_, object_, overrides_);
}

Result<void> DeleteAnnotationCommand::undo(Document& document) {
    return restoreDeletion(document, ObjectId{annotation_}, object_, overrides_);
}

Result<void> DeleteAnnotationCommand::redo(Document& document) {
    if (name_.empty()) {
        return notExecuted("redo");
    }
    return execute(document);
}

} // namespace bettercad::drawing
