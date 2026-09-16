#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/DimensionedValue.hpp>

#include <map>
#include <vector>

// Evaluation of parameter expressions.
//
// A parameter with an expression is *driven*: its value is computed from the
// parameters its expression names (see Expression for the grammar), and it
// cannot be set directly. The expression and its last evaluated value are
// both stored, so a document is complete without evaluating it.
//
// Expressions are evaluated by evaluateParameterExpressions(), which the
// regenerator runs at the start of every pass:
// - in dependency order (a parameter after every parameter it names; ties by
//   ascending ID), so the order is deterministic;
// - a parameter whose expression fails (an unknown name, a name that is not a
//   parameter, a dimension error, a division by zero) keeps its last value
//   and is reported, and driven parameters that name it are blocked and keep
//   theirs;
// - parameters whose expressions name each other (a cycle) are never
//   evaluated; they keep their values and are reported.
// Nothing is coerced: an expression must give exactly the parameter's
// dimension.
namespace bettercad {

class Document;
class Expression;

/// The parameter each name of @p expression refers to in @p document, in the
/// order of Expression::references(). A name that is not a parameter gives
/// NotFound ("unknown parameter 'x' at offset 4") or, if it names a document
/// object, InvalidArgument.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<Result<ParameterId>>
resolveExpressionNames(const Document& document, const Expression& expression);

/// Evaluates the expression of @p parameter with the current values of the
/// parameters it names; nothing is modified. The result has the parameter's
/// dimension (a negative zero is returned as zero).
///
/// Fails with NotFound if the parameter does not exist and
/// FailedPrecondition if it has no expression or its expression names the
/// parameter itself. Other failures come from the expression (see
/// Expression::parse() and Expression::evaluate()) or are DimensionMismatch
/// for a result of another dimension. Their messages start with
/// "parameter '<name>' = <expression>: ".
///
/// This function does not look for longer cycles or failed inputs; the
/// document-wide evaluateParameterExpressions() does.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<DimensionedValue> evaluateParameterExpression(const Document& document,
                                                                                         ParameterId parameter);

struct ParameterEvaluationReport {
    /// Driven parameters evaluated, in evaluation order.
    std::vector<ParameterId> evaluated{};
    /// The evaluated parameters whose value changed, in evaluation order.
    std::vector<ParameterId> changed{};
    /// Driven parameters whose evaluation failed, with the error.
    std::map<ParameterId, Error> failed{};
    /// Driven parameters not evaluated because a parameter they depend on
    /// failed, was blocked or is in a cycle; ascending.
    std::vector<ParameterId> blocked{};
    /// Groups of parameters whose expressions depend on each other, each
    /// ascending, ordered by their smallest ID. They are not evaluated.
    std::vector<std::vector<ParameterId>> cycles{};

    [[nodiscard]] bool succeeded() const noexcept { return failed.empty() && blocked.empty() && cycles.empty(); }
};

/// Evaluates every driven parameter of @p document in dependency order and
/// stores the values that changed (see the rules above). Failed, blocked and
/// cyclic parameters keep their last values. Parameter revisions, and the
/// document's, advance only for values that changed.
[[nodiscard]] BETTERCAD_CORE_EXPORT ParameterEvaluationReport evaluateParameterExpressions(Document& document);

} // namespace bettercad
