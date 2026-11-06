// P19-IC-001 -- the CFD initial field (ADR-068).
//
// ONE PATH, TWO ENTRY POINTS. `prepareCfdInitialField` and
// `cfdInitialFieldFailure` both call `prepareInternal`, which builds the field
// or names the refusal. They cannot disagree about whether a definition is
// preparable, because there is one function that decides it -- the shape
// P19-BC-001's review asked for after finding a predictor that could drift from
// the path it predicted.

#include <bettercad/cfd/CfdInitialField.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::cfd {

/// The one assembler. The constructor it calls is private and this struct is
/// its only friend, so every `PreparedCfdInitialField` in existence came
/// through here -- and, unlike `PreparedCfdBoundary`, none of them can be empty,
/// because there is no default constructor to produce an empty one.
struct CfdInitialFieldAssembler {
    [[nodiscard]] static PreparedCfdInitialField assemble(CfdInitialFieldSource source,
                                                          CfdFieldMap layout) {
        return PreparedCfdInitialField{std::move(source), std::move(layout)};
    }
};

namespace {

/// The source a prepared field would carry. One site, called by the public
/// `currentInitialFieldSource` and by `prepareInternal`, so a prepared field's
/// own source is built by the same code a consumer compares against.
[[nodiscard]] CfdInitialFieldSource sourceOf(const CfdModel& model, const CfdTopology& topology,
                                             const CfdAnalysisDefinition& definition) {
    return CfdInitialFieldSource{
        .body = model.body(),
        .mesh = topology.mesh(),
        .cells = topology.cells(),
        .initialCondition = definition.initialCondition,
    };
}

/// Either the prepared field or the refusal that stopped it. Never both, and
/// never neither.
struct Outcome {
    std::optional<CfdInitialFieldFailure> failure{};
    std::optional<PreparedCfdInitialField> field{};
};

/// The whole decision, in phase order.
[[nodiscard]] Outcome prepareInternal(const CfdModel& model, const CfdTopology& topology,
                                      const CfdAnalysisDefinition& definition) {
    // PHASE 0: IS THIS EVEN THIS ANALYSIS? The model is the CFD input of one
    // body under one mesh control, and the definition names a mesh control of
    // its own. If they disagree the caller has mixed two analyses up, and every
    // answer below would be about the wrong one -- including the array's
    // LENGTH, which is why this is the one definition member that could not be
    // omitted from the source binding with a provable reason. An adversarial
    // review found this check missing and supplied the case.
    if (definition.mesh != model.control()) {
        return Outcome{
            .failure = CfdInitialFieldFailure{
                .problem = CfdInitialFieldProblem::DefinitionNamesAnotherMesh,
                .error = Error{ErrorCode::FailedPrecondition,
                               std::format("CFD initial field: the analysis names mesh control {} "
                                           "but the CFD input was built for {}, so this is "
                                           "another analysis's initial condition",
                                           definition.mesh.value(), model.control().value())}}};
    }

    // PHASE 1: the canonical record on its own terms. `validate`'s refusal is
    // passed through rather than re-derived -- P19-DATA-001 owns the rule, and a
    // second copy of it here would be a second thing to keep true.
    if (const Result<void> checked = validate(definition.initialCondition); !checked.has_value()) {
        return Outcome{.failure = CfdInitialFieldFailure{
                           .problem = CfdInitialFieldProblem::InitialConditionNotFinite,
                           .error = checked.error()}};
    }

    // PHASE 2: the layout. The staged gate, so a topology of another mesh
    // generation is refused here and not discovered as a wrong array length
    // later.
    Result<CfdFieldMap> layout = requireCfdFieldMap(model, topology);
    if (!layout.has_value()) {
        return Outcome{
            .failure = CfdInitialFieldFailure{.problem = CfdInitialFieldProblem::LayoutUnavailable,
                                              .error = layout.error()}};
    }

    return Outcome{.field = CfdInitialFieldAssembler::assemble(sourceOf(model, topology, definition),
                                                               std::move(*layout))};
}

}   // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

std::string_view toString(CfdInitialFieldProblem problem) noexcept {
    switch (problem) {
    case CfdInitialFieldProblem::DefinitionNamesAnotherMesh:
        return "definition-names-another-mesh";
    case CfdInitialFieldProblem::InitialConditionNotFinite:
        return "initial-condition-not-finite";
    case CfdInitialFieldProblem::LayoutUnavailable:
        return "layout-unavailable";
    case CfdInitialFieldProblem::VariableIsUnknown:
        return "variable-is-unknown";
    case CfdInitialFieldProblem::VariableIsNotCellCentred:
        return "variable-is-not-cell-centred";
    case CfdInitialFieldProblem::ArrayLengthWrong:
        return "array-length-wrong";
    }
    return "unknown";
}

std::string_view toString(CfdInitialFieldStaleReason reason) noexcept {
    switch (reason) {
    case CfdInitialFieldStaleReason::Body:
        return "body";
    case CfdInitialFieldStaleReason::Mesh:
        return "mesh";
    case CfdInitialFieldStaleReason::InitialCondition:
        return "initial-condition";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The prepared field
// ---------------------------------------------------------------------------

std::size_t PreparedCfdInitialField::size(CfdVariable variable) const noexcept {
    const std::optional<CfdFieldDescriptor> descriptor = descriptorOf(variable);
    if (!descriptor.has_value() || descriptor->location != CfdFieldLocation::Cell) {
        // 0 FOR A VARIABLE THIS FIELD CANNOT INITIALISE, which is the same
        // answer as "no slots" and is never a throw. A caller that sizes an
        // array from this gets an empty one for `MassFlux`, and the expansion
        // then refuses it by name rather than quietly writing nothing.
        return 0;
    }
    return layout_.size(variable);
}

// ---------------------------------------------------------------------------
// Currency
// ---------------------------------------------------------------------------

std::vector<CfdInitialFieldStaleReason> staleReasons(const PreparedCfdInitialField& field,
                                                     const CfdInitialFieldSource& source) {
    const CfdInitialFieldSource& held = field.source();
    std::vector<CfdInitialFieldStaleReason> reasons;
    if (held.body != source.body) {
        reasons.push_back(CfdInitialFieldStaleReason::Body);
    }
    // The stamp AND the cell count: a stamp identifies the BUILDER, not the
    // snapshot. One count, because a cell-centred field cannot be changed by a
    // face count (ADR-068, Decision 7).
    if (!(held.mesh == source.mesh) || held.cells != source.cells) {
        reasons.push_back(CfdInitialFieldStaleReason::Mesh);
    }
    if (!(held.initialCondition == source.initialCondition)) {
        reasons.push_back(CfdInitialFieldStaleReason::InitialCondition);
    }
    return reasons;
}

bool isCurrent(const PreparedCfdInitialField& field, const CfdInitialFieldSource& source) {
    return staleReasons(field, source).empty();
}

CfdInitialFieldSource currentInitialFieldSource(const CfdModel& model, const CfdTopology& topology,
                                                const CfdAnalysisDefinition& definition) {
    return sourceOf(model, topology, definition);
}

// ---------------------------------------------------------------------------
// Preparing
// ---------------------------------------------------------------------------

Result<PreparedCfdInitialField> prepareCfdInitialField(const CfdModel& model,
                                                       const CfdTopology& topology,
                                                       const CfdAnalysisDefinition& definition) {
    Outcome outcome = prepareInternal(model, topology, definition);
    if (outcome.failure.has_value()) {
        return std::unexpected(outcome.failure->error);
    }
    return std::move(*outcome.field);
}

std::optional<CfdInitialFieldFailure> cfdInitialFieldFailure(const CfdModel& model,
                                                            const CfdTopology& topology,
                                                            const CfdAnalysisDefinition& definition) {
    return prepareInternal(model, topology, definition).failure;
}

std::optional<CfdInitialFieldProblem> cfdInitialFieldProblem(const CfdModel& model,
                                                             const CfdTopology& topology,
                                                             const CfdAnalysisDefinition& definition) {
    const std::optional<CfdInitialFieldFailure> failure =
        cfdInitialFieldFailure(model, topology, definition);
    if (!failure.has_value()) {
        return std::nullopt;
    }
    return failure->problem;
}

// ---------------------------------------------------------------------------
// Expanding
// ---------------------------------------------------------------------------

std::optional<CfdInitialFieldProblem>
cfdInitialFieldExpansionProblem(const PreparedCfdInitialField& field, CfdVariable variable,
                                std::size_t length) noexcept {
    const std::optional<CfdFieldDescriptor> descriptor = descriptorOf(variable);
    // UNKNOWN BEFORE LENGTH, so the tag names the variable rather than the
    // length. NOT, as an earlier comment here claimed, to prevent a silent
    // no-op: a review measured that and `(unknown, 0)` reports this problem
    // either way, while an unrecognised enumerator that reached the expansion
    // would fall out of a switch with no `default` to a named refusal. The
    // order buys the honest tag -- `(unknown, 17)` is the variable's fault, not
    // the length's.
    if (!descriptor.has_value()) {
        return CfdInitialFieldProblem::VariableIsUnknown;
    }
    if (descriptor->location != CfdFieldLocation::Cell) {
        return CfdInitialFieldProblem::VariableIsNotCellCentred;
    }
    // AGAINST `field.size()`, THE SAME QUESTION A CALLER ASKS to size its array,
    // so the two cannot disagree about what the right length is.
    if (length != field.size(variable)) {
        return CfdInitialFieldProblem::ArrayLengthWrong;
    }
    return std::nullopt;
}

std::optional<CfdInitialFieldFailure>
cfdInitialFieldExpansionFailure(const PreparedCfdInitialField& field, CfdVariable variable,
                                std::size_t length) {
    const std::optional<CfdInitialFieldProblem> problem =
        cfdInitialFieldExpansionProblem(field, variable, length);
    if (!problem.has_value()) {
        return std::nullopt;
    }
    Error error{};
    switch (*problem) {
    case CfdInitialFieldProblem::VariableIsUnknown:
        error = Error{ErrorCode::InvalidArgument,
                      std::format("CFD initial field: variable {} is not one this build defines, "
                                  "so it has no initial values",
                                  static_cast<unsigned>(variable))};
        break;
    case CfdInitialFieldProblem::VariableIsNotCellCentred:
        error = Error{
            ErrorCode::InvalidArgument,
            std::format("CFD initial field: {} is not cell-centred, and a mass flow rate is an "
                        "outcome of the velocity field rather than an initial value",
                        toString(variable))};
        break;
    case CfdInitialFieldProblem::ArrayLengthWrong:
        error = Error{ErrorCode::InvalidArgument,
                      std::format("CFD initial field: an array of {} needs {} values for {} cells, "
                                  "not {}",
                                  toString(variable), field.size(variable), field.cells(),
                                  length)};
        break;
    case CfdInitialFieldProblem::DefinitionNamesAnotherMesh:
    case CfdInitialFieldProblem::InitialConditionNotFinite:
    case CfdInitialFieldProblem::LayoutUnavailable:
        // UNREACHABLE: all three are decided while PREPARING, and a
        // `PreparedCfdInitialField` exists only if neither happened. Named
        // rather than defaulted, so adding a problem to the enum stops
        // compiling here instead of silently falling through to a blank error.
        error = Error{ErrorCode::Internal,
                      std::format("CFD initial field: {} cannot arise while expanding",
                                  toString(*problem))};
        break;
    }
    return CfdInitialFieldFailure{.problem = *problem, .error = error};
}

Result<void> expandCfdInitialField(const PreparedCfdInitialField& field, CfdVariable variable,
                                   std::span<double> values) {
    // EVERY SLOT OR NONE: the length is checked before the first write, so a
    // refusal leaves `values` exactly as the caller left it.
    if (const std::optional<CfdInitialFieldFailure> failure =
            cfdInitialFieldExpansionFailure(field, variable, values.size());
        failure.has_value()) {
        return std::unexpected(failure->error);
    }

    const CfdInitialCondition& initial = field.initialCondition();
    switch (variable) {
    case CfdVariable::Pressure:
        // UNIFORM, AND GRAVITY DOES NOT ENTER (ADR-068, Decision 6). The same
        // value in every cell, whatever height the cell sits at.
        std::ranges::fill(values, initial.pressure.si());
        return Result<void>{};
    case CfdVariable::Velocity: {
        const CfdFieldMap& layout = field.layout();
        for (std::size_t c = 0; c < layout.cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            for (const VectorComponent component : kVectorComponents) {
                // THE SUBSCRIPT COMES FROM THE LAYOUT (ADR-068, Decision 4).
                // `comp * cells + cell` is not computed here, so there is one
                // copy of the blocked numbering and it is CfdFields.cpp's.
                const std::optional<CellVectorIndex> index = layout.vectorIndex(cell, component);
                if (!index.has_value()) {
                    // UNREACHABLE: `cell` is below `layout.cells()` and
                    // `component` comes from `kVectorComponents`, which is
                    // exactly the set `vectorIndex` accepts. Diagnosed rather
                    // than asserted, because an index this loop cannot form is
                    // still a reason to refuse rather than to write a slot
                    // chosen by a fallback.
                    return makeError(
                        ErrorCode::Internal,
                        std::format("CFD initial field: the layout has no velocity slot for cell "
                                    "{} component {}",
                                    c, static_cast<unsigned>(component)));
                }
                // `CartesianVector<Q>::component` IS the component switch,
                // already typed and already -Wswitch-protected. An earlier
                // version of this file wrote a second copy of it here and
                // defended the duplication in a comment; a review pointed at
                // the abstraction that was already there, which is the rule
                // this project states.
                values[index->value()] = initial.velocity.component(component).si();
            }
        }
        return Result<void>{};
    }
    case CfdVariable::MassFlux:
        // Refused above by `cfdInitialFieldExpansionFailure`; named here so
        // `-Wswitch` has every enumerator covered.
        break;
    }
    // A CELL-CENTRED VARIABLE ADDED WITHOUT A CASE LANDS HERE AND IS REFUSED,
    // rather than returning success having written nothing. `-Wswitch` is an
    // error in every qualified preset, so this is belt and braces -- but the
    // braces are what a shared build with a looser warning set would have.
    return makeError(ErrorCode::Internal,
                     std::format("CFD initial field: {} has no expansion", toString(variable)));
}

}   // namespace bettercad::cfd
