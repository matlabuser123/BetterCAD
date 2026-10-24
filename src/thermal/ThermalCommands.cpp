// Undoable thermal-analysis edits (P18-CMD-001, ADR-055).
//
// See ThermalCommands.hpp for the authority rule this file exists to hold: the
// history carries canonical thermal intent and never a generated solver array.
// Nothing below constructs, stores or restores a mesh, a DOF map, a prepared load,
// a ThermalSystem, a solution, a post result, a balance or a validation report --
// and this translation unit includes the command header (which reaches the
// canonical analysis and its data) and the document, and no header of the solve,
// the assembly, the recovery, the balance or the validation, so none of them is
// even declared here. It calls no solve, no preparation, no assembly, no recovery
// and no validation. A number is formatted into a description for display and is
// never parsed back: every stored value is a typed canonical value.

#include <bettercad/thermal/ThermalCommands.hpp>

#include <bettercad/core/document/Document.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::thermal {
namespace {

[[nodiscard]] std::unexpected<Error> notExecuted(std::string_view what) {
    return std::unexpected(
        Error{ErrorCode::FailedPrecondition,
              std::format("cannot {} a thermal command that has not been executed", what)});
}

[[nodiscard]] std::unexpected<Error> noSuchAnalysis(ThermalAnalysisId analysis) {
    return std::unexpected(Error{
        ErrorCode::NotFound, std::format("there is no thermal analysis {}", analysis.value())});
}

/// Applies @p definition to the analysis, through the document.
///
/// THROUGH Document::modifyObject, which type-checks the object, validates via
/// ThermalAnalysis::setDefinition before assigning, and bumps the object and
/// document revisions ONLY when something actually changed. A command that reached
/// into the object directly would bypass all three -- including the revision bump
/// that keeps a revision-keyed record honest.
[[nodiscard]] Result<bool> apply(Document& document, ThermalAnalysisId analysis,
                                 const ThermalAnalysisDefinition& definition) {
    return document.modifyObject<ThermalAnalysis>(
        ObjectId::fromValue(analysis.value()),
        [&definition](ThermalAnalysis& object) { return object.setDefinition(definition); });
}

[[nodiscard]] auto findLoad(std::vector<ThermalLoad>& loads, ThermalLoadId id) {
    return std::ranges::find_if(loads, [id](const ThermalLoad& load) { return load.id() == id; });
}

[[nodiscard]] auto findCondition(std::vector<ThermalBoundaryCondition>& conditions,
                                 ThermalBoundaryConditionId id) {
    return std::ranges::find_if(conditions, [id](const ThermalBoundaryCondition& condition) {
        return condition.id() == id;
    });
}

/// A canonical target must NAME A FEATURE. The data model validates a face SELECTOR (a side face needs an entity) but accepts
/// a `FaceName` whose feature is the invalid default handle, which names no face of anything and would only ever be reported
/// unresolved. A command refuses it at the boundary: an invalid handle is not a canonical target representation. A handle that
/// is VALID and names a feature that does not exist (or no longer exists) is still accepted -- whether a face resolves is a
/// question about a document and a mesh, answered by preparation, and a load may be defined before any mesh exists.
[[nodiscard]] Result<void> requireNamedFeature(const FaceName& face, std::string_view what) {
    if (!face.feature.isValid()) {
        return std::unexpected(Error{
            ErrorCode::InvalidArgument,
            std::format("{} targets a face of no feature (the invalid feature handle); a thermal target is a FaceName "
                        "that names a feature",
                        what)});
    }
    return Result<void>{};
}

[[nodiscard]] Result<void> requireNamedTarget(const ThermalLoad& load) {
    if (const std::optional<FaceName> face = load.target(); face.has_value()) {
        return requireNamedFeature(*face, "a load");
    }
    return Result<void>{};   // a volumetric source has no face
}

[[nodiscard]] Result<void> requireNamedTarget(const ThermalBoundaryCondition& condition) {
    return requireNamedFeature(condition.face(), "a boundary condition");
}

[[nodiscard]] std::unexpected<Error> divergedFrom(std::string_view what, ThermalAnalysisId analysis) {
    return std::unexpected(Error{
        ErrorCode::FailedPrecondition,
        std::format("cannot {} the edit of thermal analysis {}: the analysis was changed outside the command history, and "
                    "assigning the stored definition would silently discard that change",
                    what, analysis.value())});
}

[[nodiscard]] std::unexpected<Error> steadyHasNo(ThermalAnalysisId analysis, std::string_view what) {
    return std::unexpected(Error{
        ErrorCode::FailedPrecondition,
        std::format("thermal analysis {} is steady and a steady analysis has no {}; the data "
                    "model refuses the field as inert intent whose edit would stale a result for "
                    "nothing",
                    analysis.value(), what)});
}

} // namespace

// ---------------------------------------------------------------------------
// Create
// ---------------------------------------------------------------------------

CreateThermalAnalysisCommand::CreateThermalAnalysisCommand(std::string name,
                                                           ThermalAnalysisDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateThermalAnalysisCommand::description() const {
    return std::format("Create thermal analysis '{}'", name_);
}

Result<void> CreateThermalAnalysisCommand::execute(Document& document) {
    // ThermalAnalysis::create validates before anything is built, so an invalid
    // definition fails here and the document is never touched. Because
    // CommandHistory records only a command whose execute() succeeded, the failed
    // attempt does not reach the history either.
    for (const ThermalLoad& load : definition_.loads) {
        if (const Result<void> named = requireNamedTarget(load); !named.has_value()) {
            return std::unexpected(named.error());
        }
    }
    for (const ThermalBoundaryCondition& condition : definition_.boundaryConditions) {
        if (const Result<void> named = requireNamedTarget(condition); !named.has_value()) {
            return std::unexpected(named.error());
        }
    }
    auto analysis = ThermalAnalysis::create(name_, definition_);
    if (!analysis.has_value()) {
        return std::unexpected(analysis.error());
    }
    add_.emplace(std::move(*analysis));
    if (auto added = add_->execute(document); !added.has_value()) {
        // Undo the emplace so a second attempt starts clean rather than reusing a
        // half-executed AddObjectCommand.
        add_.reset();
        return added;
    }
    return Result<void>{};
}

Result<void> CreateThermalAnalysisCommand::undo(Document& document) {
    if (!add_.has_value()) {
        return notExecuted("undo");
    }
    return add_->undo(document);
}

Result<void> CreateThermalAnalysisCommand::redo(Document& document) {
    if (!add_.has_value()) {
        return notExecuted("redo");
    }
    // THE SAME ThermalAnalysisId, because AddObjectCommand restores the prototype's
    // recorded ID rather than minting a new one. A result records which analysis it
    // came from, so a redo that renumbered would orphan it.
    return add_->redo(document);
}

ThermalAnalysisId CreateThermalAnalysisCommand::analysisId() const noexcept {
    return add_.has_value() ? ThermalAnalysisId::fromValue(add_->objectId().value())
                            : ThermalAnalysisId{};
}

// ---------------------------------------------------------------------------
// The edit base
// ---------------------------------------------------------------------------

Result<void> ThermalAnalysisEditCommand::execute(Document& document) {
    const ThermalAnalysis* object = findThermalAnalysis(document, analysis_);
    if (object == nullptr) {
        return noSuchAnalysis(analysis_);
    }
    const ThermalAnalysisDefinition before = object->definition();
    Result<ThermalAnalysisDefinition> after = edit(before);
    if (!after.has_value()) {
        return std::unexpected(after.error());
    }
    // STAGED BEFORE THE DOCUMENT IS TOUCHED, so that an allocation failure while copying the two definitions leaves the
    // document unchanged and the command unexecuted; and unstaged again if the apply is refused, so a refused edit leaves
    // before_ unset -- which is what makes CommandHistory record nothing and what makes undo refuse.
    before_ = before;
    after_ = *after;
    Result<bool> changed = apply(document, analysis_, *after);
    if (!changed.has_value()) {
        before_.reset();
        after_.reset();
        return std::unexpected(changed.error());
    }
    changed_ = *changed;
    if (!changed_) {
        // NO EFFECTIVE CHANGE (the document's own equality: a signed zero equals its negation, so +0.0 -> -0.0 is a
        // no-op), so the document still holds `before` and that is what this command installed. Recording the requested
        // value instead would make after() disagree with the document.
        after_ = before;
    }
    return Result<void>{};
}

Result<void> ThermalAnalysisEditCommand::undo(Document& document) {
    if (!before_.has_value()) {
        return notExecuted("undo");
    }
    const ThermalAnalysis* object = findThermalAnalysis(document, analysis_);
    if (object == nullptr) {
        return noSuchAnalysis(analysis_);
    }
    // THE STACK DISCIPLINE, CHECKED. CommandHistory undoes in reverse order, so the analysis holds exactly the definition
    // this command installed. If it does not, something edited it outside the history, and assigning `before_` would
    // silently take that change with it: refuse, and leave the document and the stacks as they are.
    if (!(object->definition() == *after_)) {
        return divergedFrom("undo", analysis_);
    }
    // EXACTLY the definition that was there, as one value. Nothing is reconstructed field by field and nothing is
    // inverted, so there is no field to forget and no arithmetic to drift.
    Result<bool> restored = apply(document, analysis_, *before_);
    if (!restored.has_value()) {
        return std::unexpected(restored.error());
    }
    return Result<void>{};
}

Result<void> ThermalAnalysisEditCommand::redo(Document& document) {
    if (!after_.has_value()) {
        return notExecuted("redo");
    }
    const ThermalAnalysis* object = findThermalAnalysis(document, analysis_);
    if (object == nullptr) {
        return noSuchAnalysis(analysis_);
    }
    if (!(object->definition() == *before_)) {
        return divergedFrom("redo", analysis_);
    }
    Result<bool> reapplied = apply(document, analysis_, *after_);
    if (!reapplied.has_value()) {
        return std::unexpected(reapplied.error());
    }
    return Result<void>{};
}

// ---------------------------------------------------------------------------
// Loads
// ---------------------------------------------------------------------------

AddThermalLoadCommand::AddThermalLoadCommand(ThermalAnalysisId analysis, ThermalLoad load)
    : ThermalAnalysisEditCommand(analysis), load_(std::move(load)) {}

std::string AddThermalLoadCommand::description() const {
    return std::format("Add {} load {}", toString(load_.kind()), load_.id().value());
}

Result<ThermalAnalysisDefinition>
AddThermalLoadCommand::edit(const ThermalAnalysisDefinition& before) const {
    // THE RECORD'S OWN VALIDATOR, which refuses a NaN value and an invalid identity
    // without consulting a mesh. Whether the target RESOLVES is a question about a
    // particular mesh and belongs to preparation.
    if (const Result<void> checked = validate(load_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (const Result<void> named = requireNamedTarget(load_); !named.has_value()) {
        return std::unexpected(named.error());
    }
    ThermalAnalysisDefinition after = before;
    if (findLoad(after.loads, load_.id()) != after.loads.end()) {
        return std::unexpected(Error{
            ErrorCode::AlreadyExists,
            std::format("thermal analysis {} already has a load {}", analysis_.value(),
                        load_.id().value())});
    }
    // APPENDED: the order is the user's and this layer does not reorder it.
    after.loads.push_back(load_);
    return after;
}

EditThermalLoadCommand::EditThermalLoadCommand(ThermalAnalysisId analysis, ThermalLoad replacement)
    : ThermalAnalysisEditCommand(analysis), replacement_(std::move(replacement)) {}

std::string EditThermalLoadCommand::description() const {
    return std::format("Edit load {}", replacement_.id().value());
}

Result<ThermalAnalysisDefinition>
EditThermalLoadCommand::edit(const ThermalAnalysisDefinition& before) const {
    if (const Result<void> checked = validate(replacement_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (const Result<void> named = requireNamedTarget(replacement_); !named.has_value()) {
        return std::unexpected(named.error());
    }
    ThermalAnalysisDefinition after = before;
    const auto at = findLoad(after.loads, replacement_.id());
    if (at == after.loads.end()) {
        // REFUSED, NOT CREATED, and found by the STABLE ID rather than by a
        // position: an edit whose target is gone is a mistake to report, and
        // silently adding the load would turn a stale dialog into an invented one.
        return std::unexpected(Error{
            ErrorCode::NotFound, std::format("thermal analysis {} has no load {}", analysis_.value(),
                                             replacement_.id().value())});
    }
    // IN PLACE, so the user's order survives an edit -- and the WHOLE record is
    // replaced, so a multi-field edit cannot leave one field changed.
    *at = replacement_;
    return after;
}

RemoveThermalLoadCommand::RemoveThermalLoadCommand(ThermalAnalysisId analysis,
                                                   ThermalLoadId load) noexcept
    : ThermalAnalysisEditCommand(analysis), load_(load) {}

std::string RemoveThermalLoadCommand::description() const {
    return std::format("Remove load {}", load_.value());
}

Result<ThermalAnalysisDefinition>
RemoveThermalLoadCommand::edit(const ThermalAnalysisDefinition& before) const {
    ThermalAnalysisDefinition after = before;
    const auto at = findLoad(after.loads, load_);
    if (at == after.loads.end()) {
        return std::unexpected(Error{
            ErrorCode::NotFound,
            std::format("thermal analysis {} has no load {}", analysis_.value(), load_.value())});
    }
    after.loads.erase(at);
    return after;
}

// ---------------------------------------------------------------------------
// Boundary conditions
// ---------------------------------------------------------------------------

AddThermalBoundaryConditionCommand::AddThermalBoundaryConditionCommand(
    ThermalAnalysisId analysis, ThermalBoundaryCondition condition)
    : ThermalAnalysisEditCommand(analysis), condition_(std::move(condition)) {}

std::string AddThermalBoundaryConditionCommand::description() const {
    return std::format("Add {} boundary condition {}", toString(condition_.kind()),
                       condition_.id().value());
}

Result<ThermalAnalysisDefinition>
AddThermalBoundaryConditionCommand::edit(const ThermalAnalysisDefinition& before) const {
    // P18-DATA's own validator: a finite absolute temperature and, for convection, a
    // finite positive coefficient. No mesh is consulted, and no conflict policy is
    // invented -- two conditions on one face are canonical intent this layer
    // permits; what they mean is P18-BC's and P18-VALID's to say.
    if (const Result<void> checked = validate(condition_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (const Result<void> named = requireNamedTarget(condition_); !named.has_value()) {
        return std::unexpected(named.error());
    }
    ThermalAnalysisDefinition after = before;
    if (findCondition(after.boundaryConditions, condition_.id()) != after.boundaryConditions.end()) {
        return std::unexpected(Error{
            ErrorCode::AlreadyExists,
            std::format("thermal analysis {} already has a boundary condition {}", analysis_.value(),
                        condition_.id().value())});
    }
    after.boundaryConditions.push_back(condition_);
    return after;
}

EditThermalBoundaryConditionCommand::EditThermalBoundaryConditionCommand(
    ThermalAnalysisId analysis, ThermalBoundaryCondition replacement)
    : ThermalAnalysisEditCommand(analysis), replacement_(std::move(replacement)) {}

std::string EditThermalBoundaryConditionCommand::description() const {
    return std::format("Edit boundary condition {}", replacement_.id().value());
}

Result<ThermalAnalysisDefinition>
EditThermalBoundaryConditionCommand::edit(const ThermalAnalysisDefinition& before) const {
    if (const Result<void> checked = validate(replacement_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (const Result<void> named = requireNamedTarget(replacement_); !named.has_value()) {
        return std::unexpected(named.error());
    }
    ThermalAnalysisDefinition after = before;
    const auto at = findCondition(after.boundaryConditions, replacement_.id());
    if (at == after.boundaryConditions.end()) {
        return std::unexpected(Error{
            ErrorCode::NotFound,
            std::format("thermal analysis {} has no boundary condition {}", analysis_.value(),
                        replacement_.id().value())});
    }
    // THE FACE AND EVERY VALUE TOGETHER, as one validated record. There is no
    // geometry rebinding here: a FaceName is assigned as it was given.
    *at = replacement_;
    return after;
}

RemoveThermalBoundaryConditionCommand::RemoveThermalBoundaryConditionCommand(
    ThermalAnalysisId analysis, ThermalBoundaryConditionId condition) noexcept
    : ThermalAnalysisEditCommand(analysis), condition_(condition) {}

std::string RemoveThermalBoundaryConditionCommand::description() const {
    return std::format("Remove boundary condition {}", condition_.value());
}

Result<ThermalAnalysisDefinition>
RemoveThermalBoundaryConditionCommand::edit(const ThermalAnalysisDefinition& before) const {
    ThermalAnalysisDefinition after = before;
    const auto at = findCondition(after.boundaryConditions, condition_);
    if (at == after.boundaryConditions.end()) {
        return std::unexpected(Error{
            ErrorCode::NotFound, std::format("thermal analysis {} has no boundary condition {}",
                                             analysis_.value(), condition_.value())});
    }
    after.boundaryConditions.erase(at);
    return after;
}

// ---------------------------------------------------------------------------
// Initial temperature
// ---------------------------------------------------------------------------

SetInitialTemperatureCommand::SetInitialTemperatureCommand(ThermalAnalysisId analysis,
                                                           UniformInitialTemperature initial) noexcept
    : ThermalAnalysisEditCommand(analysis), initial_(initial) {}

std::string SetInitialTemperatureCommand::description() const {
    return std::format("Set initial temperature ({} K)", initial_.value.si());
}

Result<ThermalAnalysisDefinition>
SetInitialTemperatureCommand::edit(const ThermalAnalysisDefinition& before) const {
    // THE MODE FIRST: a steady analysis cannot take the field whatever its value, and saying so is more useful than
    // reporting a defect of a value that could never have been applied.
    if (before.mode != ThermalAnalysisMode::Transient) {
        return steadyHasNo(analysis_, "initial temperature");
    }
    // P18-DATA's own validator: finite and not below 0 K. No Celsius, no rounding.
    if (const Result<void> checked = validate(initial_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    ThermalAnalysisDefinition after = before;
    after.initialCondition = initial_;
    return after;
}

// ---------------------------------------------------------------------------
// Solver settings
// ---------------------------------------------------------------------------

SetThermalSolverSettingsCommand::SetThermalSolverSettingsCommand(
    ThermalAnalysisId analysis, ThermalSolverSettings settings) noexcept
    : ThermalAnalysisEditCommand(analysis), settings_(settings) {}

std::string SetThermalSolverSettingsCommand::description() const {
    return std::format("Set thermal solver settings (residual {}, pivot floor {})",
                       settings_.relativeResidualTolerance, settings_.pivotFloor);
}

Result<ThermalAnalysisDefinition>
SetThermalSolverSettingsCommand::edit(const ThermalAnalysisDefinition& before) const {
    // P18-DATA's own validator. A NaN or non-positive tolerance is refused HERE
    // rather than left to decide something inside a comparison later; checked in
    // `edit` as well as by validate(definition) so the diagnostic names the
    // settings. Both tolerances apply together or not at all.
    if (const Result<void> checked = validate(settings_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    ThermalAnalysisDefinition after = before;
    after.solver = settings_;
    return after;
}

// ---------------------------------------------------------------------------
// Transient settings
// ---------------------------------------------------------------------------

SetTransientSettingsCommand::SetTransientSettingsCommand(ThermalAnalysisId analysis,
                                                         TransientSettings settings) noexcept
    : ThermalAnalysisEditCommand(analysis), settings_(settings) {}

std::string SetTransientSettingsCommand::description() const {
    return std::format("Set transient settings (step {} s, end {} s, {})", settings_.timeStep.si(),
                       settings_.endTime.si(), toString(settings_.output));
}

Result<ThermalAnalysisDefinition>
SetTransientSettingsCommand::edit(const ThermalAnalysisDefinition& before) const {
    // THE MODE FIRST, for the reason SetInitialTemperatureCommand gives.
    if (before.mode != ThermalAnalysisMode::Transient) {
        return steadyHasNo(analysis_, "time step or end time");
    }
    // P18-DATA's own validator: 0 < step <= end, both finite. A refused value is never corrected, and the three fields
    // land together or none does.
    if (const Result<void> checked = validate(settings_); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    ThermalAnalysisDefinition after = before;
    after.transient = settings_;
    return after;
}

} // namespace bettercad::thermal
