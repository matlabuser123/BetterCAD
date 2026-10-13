// Undoable structural-analysis edits (P17-CMD-001, ADR-047).
//
// See StructuralCommands.hpp for the authority rule this file exists to hold:
// the history carries canonical engineering intent and never a generated
// solver array. Nothing below constructs, stores or restores a
// GlobalStructuralSystem, a StructuralSolution, a StructuralPostResult, a
// StructuralReactionResult, a ConstraintSet or a prepared nodal force field --
// and this translation unit does not include any of their headers.

#include <bettercad/structural/StructuralCommands.hpp>

#include <bettercad/core/document/Document.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::structural {
namespace {

[[nodiscard]] std::unexpected<Error> notExecuted(std::string_view what) {
    return std::unexpected(
        Error{ErrorCode::FailedPrecondition,
              std::format("cannot {} a structural command that has not been executed", what)});
}

[[nodiscard]] std::unexpected<Error> noSuchAnalysis(AnalysisId analysis) {
    return std::unexpected(Error{
        ErrorCode::NotFound, std::format("there is no structural analysis {}", analysis.value())});
}

/// Applies @p definition to the analysis, through the document.
///
/// THROUGH Document::modifyObject, which type-checks the object, validates via
/// StructuralAnalysis::setDefinition before assigning, and bumps the object and
/// document revisions ONLY when something actually changed. A command that
/// reached into the object directly would bypass all three -- including the
/// revision bump that is what makes a previously computed result stale.
[[nodiscard]] Result<bool> apply(Document& document, AnalysisId analysis,
                                 const StructuralAnalysisDefinition& definition) {
    return document.modifyObject<StructuralAnalysis>(
        ObjectId::fromValue(analysis.value()),
        [&definition](StructuralAnalysis& object) { return object.setDefinition(definition); });
}

[[nodiscard]] auto findLoad(std::vector<StructuralLoad>& loads, LoadId id) {
    return std::ranges::find_if(loads,
                                [id](const StructuralLoad& load) { return load.id() == id; });
}

[[nodiscard]] auto findRestraint(std::vector<StructuralRestraint>& restraints, RestraintId id) {
    return std::ranges::find_if(
        restraints, [id](const StructuralRestraint& restraint) { return restraint.id() == id; });
}

} // namespace

// ---------------------------------------------------------------------------
// Create and delete
// ---------------------------------------------------------------------------

CreateStructuralAnalysisCommand::CreateStructuralAnalysisCommand(
    std::string name, StructuralAnalysisDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateStructuralAnalysisCommand::description() const {
    return std::format("Create structural analysis '{}'", name_);
}

Result<void> CreateStructuralAnalysisCommand::execute(Document& document) {
    // StructuralAnalysis::create validates before anything is built, so an
    // invalid definition -- including unusable solver settings -- fails here and
    // the document is never touched. Because CommandHistory records only a
    // command whose execute() succeeded, the failed attempt does not reach the
    // history either.
    auto analysis = StructuralAnalysis::create(name_, definition_);
    if (!analysis.has_value()) {
        return std::unexpected(analysis.error());
    }
    add_.emplace(std::move(*analysis));
    if (auto added = add_->execute(document); !added.has_value()) {
        // Undo the emplace so a second attempt starts clean rather than reusing
        // a half-executed AddObjectCommand.
        add_.reset();
        return added;
    }
    return Result<void>{};
}

Result<void> CreateStructuralAnalysisCommand::undo(Document& document) {
    if (!add_.has_value()) {
        return notExecuted("undo");
    }
    return add_->undo(document);
}

Result<void> CreateStructuralAnalysisCommand::redo(Document& document) {
    if (!add_.has_value()) {
        return notExecuted("redo");
    }
    // THE SAME AnalysisId, because AddObjectCommand restores the prototype's
    // recorded ID rather than minting a new one. A result records which
    // analysis it came from, so a redo that renumbered would orphan it.
    return add_->redo(document);
}

AnalysisId CreateStructuralAnalysisCommand::analysisId() const noexcept {
    return add_.has_value() ? AnalysisId::fromValue(add_->objectId().value()) : AnalysisId{};
}

DeleteStructuralAnalysisCommand::DeleteStructuralAnalysisCommand(AnalysisId analysis) noexcept
    : analysis_(analysis) {}

std::string DeleteStructuralAnalysisCommand::description() const {
    return std::format("Delete structural analysis {}", analysis_.value());
}

Result<void> DeleteStructuralAnalysisCommand::execute(Document& document) {
    if (findStructuralAnalysis(document, analysis_) == nullptr) {
        return noSuchAnalysis(analysis_);
    }
    remove_.emplace(ObjectId::fromValue(analysis_.value()));
    if (auto removed = remove_->execute(document); !removed.has_value()) {
        remove_.reset();
        return removed;
    }
    return Result<void>{};
}

Result<void> DeleteStructuralAnalysisCommand::undo(Document& document) {
    if (!remove_.has_value()) {
        return notExecuted("undo");
    }
    // The WHOLE analysis comes back, loads and restraints and settings
    // included, because the definition owns them by value. There is no
    // orphaned load to re-attach.
    return remove_->undo(document);
}

Result<void> DeleteStructuralAnalysisCommand::redo(Document& document) {
    if (!remove_.has_value()) {
        return notExecuted("redo");
    }
    return remove_->redo(document);
}

// ---------------------------------------------------------------------------
// The edit base
// ---------------------------------------------------------------------------

Result<void> StructuralAnalysisEditCommand::execute(Document& document) {
    const StructuralAnalysis* object = findStructuralAnalysis(document, analysis_);
    if (object == nullptr) {
        return noSuchAnalysis(analysis_);
    }
    // Captured BEFORE the edit, and recorded only if the edit succeeds, so a
    // refused edit leaves before_ unset and the command unexecuted.
    const StructuralAnalysisDefinition before = object->definition();
    Result<StructuralAnalysisDefinition> after = edit(before);
    if (!after.has_value()) {
        return std::unexpected(after.error());
    }
    Result<bool> changed = apply(document, analysis_, *after);
    if (!changed.has_value()) {
        return std::unexpected(changed.error());
    }
    before_ = before;
    after_ = std::move(*after);
    changed_ = *changed;
    return Result<void>{};
}

Result<void> StructuralAnalysisEditCommand::undo(Document& document) {
    if (!before_.has_value()) {
        return notExecuted("undo");
    }
    // EXACTLY the definition that was there, as one value. Nothing is
    // reconstructed field by field and nothing is inverted, so there is no
    // field to forget and no arithmetic to drift.
    Result<bool> restored = apply(document, analysis_, *before_);
    if (!restored.has_value()) {
        return std::unexpected(restored.error());
    }
    return Result<void>{};
}

Result<void> StructuralAnalysisEditCommand::redo(Document& document) {
    if (!after_.has_value()) {
        return notExecuted("redo");
    }
    Result<bool> reapplied = apply(document, analysis_, *after_);
    if (!reapplied.has_value()) {
        return std::unexpected(reapplied.error());
    }
    return Result<void>{};
}

// ---------------------------------------------------------------------------
// Whole-definition edit
// ---------------------------------------------------------------------------

SetStructuralAnalysisDefinitionCommand::SetStructuralAnalysisDefinitionCommand(
    AnalysisId analysis, StructuralAnalysisDefinition definition)
    : StructuralAnalysisEditCommand(analysis), definition_(std::move(definition)) {}

std::string SetStructuralAnalysisDefinitionCommand::description() const {
    return std::format("Edit structural analysis {}", analysis_.value());
}

Result<StructuralAnalysisDefinition> SetStructuralAnalysisDefinitionCommand::edit(
    const StructuralAnalysisDefinition& /*before*/) const {
    return definition_;
}

// ---------------------------------------------------------------------------
// Loads
// ---------------------------------------------------------------------------

AddStructuralLoadCommand::AddStructuralLoadCommand(AnalysisId analysis, StructuralLoad load)
    : StructuralAnalysisEditCommand(analysis), load_(std::move(load)) {}

std::string AddStructuralLoadCommand::description() const {
    return std::format("Add {} load {}", toString(load_.kind()), load_.id().value());
}

Result<StructuralAnalysisDefinition> AddStructuralLoadCommand::edit(
    const StructuralAnalysisDefinition& before) const {
    // THE RECORD'S OWN VALIDATOR, which refuses a NaN value and an invalid
    // identity without consulting a mesh. Whether the target RESOLVES is a
    // question about a particular mesh and belongs to preparation -- a user may
    // legitimately define a load before a mesh exists.
    if (const Result<void> checked = validate(load_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    StructuralAnalysisDefinition after = before;
    if (findLoad(after.loads, load_.id()) != after.loads.end()) {
        return std::unexpected(
            Error{ErrorCode::AlreadyExists,
                  std::format("analysis {} already has a load {}", analysis_.value(),
                              load_.id().value())});
    }
    // APPENDED: the order is the user's and this layer does not reorder it.
    after.loads.push_back(load_);
    return after;
}

EditStructuralLoadCommand::EditStructuralLoadCommand(AnalysisId analysis,
                                                     StructuralLoad replacement)
    : StructuralAnalysisEditCommand(analysis), replacement_(std::move(replacement)) {}

std::string EditStructuralLoadCommand::description() const {
    return std::format("Edit load {}", replacement_.id().value());
}

Result<StructuralAnalysisDefinition> EditStructuralLoadCommand::edit(
    const StructuralAnalysisDefinition& before) const {
    if (const Result<void> checked = validate(replacement_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    StructuralAnalysisDefinition after = before;
    const auto at = findLoad(after.loads, replacement_.id());
    if (at == after.loads.end()) {
        // REFUSED, NOT CREATED. An edit whose target is gone is a mistake to
        // report, and silently adding the load would turn a stale dialog into
        // an invented one.
        return std::unexpected(Error{ErrorCode::NotFound,
                                     std::format("analysis {} has no load {}", analysis_.value(),
                                                 replacement_.id().value())});
    }
    // IN PLACE, so the user's order survives an edit -- and the WHOLE record is
    // replaced, so a multi-field edit cannot leave one field changed.
    *at = replacement_;
    return after;
}

RemoveStructuralLoadCommand::RemoveStructuralLoadCommand(AnalysisId analysis,
                                                         LoadId load) noexcept
    : StructuralAnalysisEditCommand(analysis), load_(load) {}

std::string RemoveStructuralLoadCommand::description() const {
    return std::format("Remove load {}", load_.value());
}

Result<StructuralAnalysisDefinition> RemoveStructuralLoadCommand::edit(
    const StructuralAnalysisDefinition& before) const {
    StructuralAnalysisDefinition after = before;
    const auto at = findLoad(after.loads, load_);
    if (at == after.loads.end()) {
        return std::unexpected(Error{
            ErrorCode::NotFound,
            std::format("analysis {} has no load {}", analysis_.value(), load_.value())});
    }
    after.loads.erase(at);
    return after;
}

// ---------------------------------------------------------------------------
// Restraints
// ---------------------------------------------------------------------------

AddStructuralRestraintCommand::AddStructuralRestraintCommand(AnalysisId analysis,
                                                             StructuralRestraint restraint)
    : StructuralAnalysisEditCommand(analysis), restraint_(std::move(restraint)) {}

std::string AddStructuralRestraintCommand::description() const {
    return std::format("Add restraint {} ({})", restraint_.id().value(),
                       toString(restraint_.components()));
}

Result<StructuralAnalysisDefinition> AddStructuralRestraintCommand::edit(
    const StructuralAnalysisDefinition& before) const {
    // P17-BC-001's own validator, which refuses an empty component mask among
    // other things and deliberately does not consult a mesh.
    if (const Result<void> checked = validate(restraint_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    StructuralAnalysisDefinition after = before;
    if (findRestraint(after.restraints, restraint_.id()) != after.restraints.end()) {
        return std::unexpected(
            Error{ErrorCode::AlreadyExists,
                  std::format("analysis {} already has a restraint {}", analysis_.value(),
                              restraint_.id().value())});
    }
    after.restraints.push_back(restraint_);
    return after;
}

EditStructuralRestraintCommand::EditStructuralRestraintCommand(AnalysisId analysis,
                                                               StructuralRestraint replacement)
    : StructuralAnalysisEditCommand(analysis), replacement_(std::move(replacement)) {}

std::string EditStructuralRestraintCommand::description() const {
    return std::format("Edit restraint {}", replacement_.id().value());
}

Result<StructuralAnalysisDefinition> EditStructuralRestraintCommand::edit(
    const StructuralAnalysisDefinition& before) const {
    if (const Result<void> checked = validate(replacement_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    StructuralAnalysisDefinition after = before;
    const auto at = findRestraint(after.restraints, replacement_.id());
    if (at == after.restraints.end()) {
        return std::unexpected(Error{ErrorCode::NotFound,
                                     std::format("analysis {} has no restraint {}",
                                                 analysis_.value(),
                                                 replacement_.id().value())});
    }
    // THE FACE AND THE COMPONENTS TOGETHER, as one validated record. There is
    // no geometry rebinding here: a FaceName is assigned as it was given.
    *at = replacement_;
    return after;
}

RemoveStructuralRestraintCommand::RemoveStructuralRestraintCommand(
    AnalysisId analysis, RestraintId restraint) noexcept
    : StructuralAnalysisEditCommand(analysis), restraint_(restraint) {}

std::string RemoveStructuralRestraintCommand::description() const {
    return std::format("Remove restraint {}", restraint_.value());
}

Result<StructuralAnalysisDefinition> RemoveStructuralRestraintCommand::edit(
    const StructuralAnalysisDefinition& before) const {
    StructuralAnalysisDefinition after = before;
    const auto at = findRestraint(after.restraints, restraint_);
    if (at == after.restraints.end()) {
        return std::unexpected(Error{
            ErrorCode::NotFound,
            std::format("analysis {} has no restraint {}", analysis_.value(), restraint_.value())});
    }
    after.restraints.erase(at);
    return after;
}

// ---------------------------------------------------------------------------
// Solver settings
// ---------------------------------------------------------------------------

SetSolverSettingsCommand::SetSolverSettingsCommand(AnalysisId analysis,
                                                   SolverSettings settings) noexcept
    : StructuralAnalysisEditCommand(analysis), settings_(settings) {}

std::string SetSolverSettingsCommand::description() const {
    return std::format("Set solver settings ({})", toString(settings_.algorithm));
}

Result<StructuralAnalysisDefinition> SetSolverSettingsCommand::edit(
    const StructuralAnalysisDefinition& before) const {
    // P17-SOLVE-001's own validator. A NaN or non-positive tolerance is refused
    // HERE rather than left to decide something inside a comparison later.
    //
    // Checked in `edit` rather than relying on `setDefinition` alone so the
    // diagnostic names the settings, though validate(definition) would catch it
    // too -- the command is refused either way and the document is untouched.
    if (const Result<void> checked = validate(settings_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    StructuralAnalysisDefinition after = before;
    after.solver = settings_;
    return after;
}

} // namespace bettercad::structural
