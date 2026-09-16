#include <bettercad/core/document/ParameterExpressions.hpp>

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Expression.hpp>
#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace bettercad {

namespace {

std::unexpected<Error> inParameter(const Parameter& parameter, const Error& error) {
    return makeError(error.code, std::format("parameter '{}' = {}: {}", parameter.name(),
                                             parameter.expression().value_or(std::string{}), error.message));
}

/// The parameter @p name refers to, without an offset in the message.
Result<ParameterId> resolveName(const Document& document, std::string_view name) {
    if (const Parameter* parameter = document.parameters().findByName(name)) {
        return parameter->id();
    }
    if (const DocumentObject* object = document.findObjectByName(name)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not a parameter but a document object of type '{}'", name,
                                     object->typeName()));
    }
    return makeError(ErrorCode::NotFound, std::format("unknown parameter '{}'", name));
}

/// The driven parameter @p id names, if it is one.
std::optional<ParameterId> drivenParameter(const Document& document, ObjectId id) {
    const std::optional<ParameterId> parameter = document.asParameter(id);
    if (parameter && document.parameters().find(*parameter)->expression()) {
        return parameter;
    }
    return std::nullopt;
}

} // namespace

/// The one way an evaluated value gets into a parameter (Document's friend).
class ParameterExpressionWriter {
public:
    static Result<bool> store(Document& document, ParameterId parameter, const DimensionedValue& value) {
        return document.storeExpressionValue(parameter, value);
    }
};

std::vector<Result<ParameterId>> resolveExpressionNames(const Document& document, const Expression& expression) {
    std::vector<Result<ParameterId>> resolved;
    resolved.reserve(expression.references().size());
    for (const std::string& name : expression.references()) {
        Result<ParameterId> id = resolveName(document, name);
        if (!id) {
            id = makeError(id.error().code,
                           std::format("{} at offset {}", id.error().message, expression.offsetOf(name)));
        }
        resolved.push_back(std::move(id));
    }
    return resolved;
}

Result<DimensionedValue> evaluateParameterExpression(const Document& document, ParameterId id) {
    const Parameter* parameter = document.parameters().find(id);
    if (parameter == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} does not exist", id));
    }
    if (!parameter->expression()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("parameter '{}' has no expression", parameter->name()));
    }
    const auto expression = Expression::parse(*parameter->expression());
    if (!expression) {
        return inParameter(*parameter, expression.error());
    }

    const auto resolve = [&](std::string_view name) -> Result<DimensionedValue> {
        const auto target = resolveName(document, name);
        if (!target) {
            return std::unexpected(target.error());
        }
        if (*target == id) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the expression uses the parameter '{}' itself", name));
        }
        const Parameter* input = document.parameters().find(*target);
        return DimensionedValue{input->dimension(), input->siValue()};
    };
    auto value = expression->evaluate(resolve);
    if (!value) {
        return inParameter(*parameter, value.error());
    }
    if (value->dimension != parameter->dimension()) {
        return inParameter(*parameter,
                           Error{ErrorCode::DimensionMismatch,
                                 std::format("the result has dimension {}, but the parameter has dimension {}",
                                             describeDimension(value->dimension),
                                             describeDimension(parameter->dimension()))});
    }
    if (value->siValue == 0.0) {
        value->siValue = 0.0; // never store a negative zero
    }
    return value;
}

ParameterEvaluationReport evaluateParameterExpressions(Document& document) {
    ParameterEvaluationReport report;
    const DocumentGraph documentGraph = buildDependencyGraph(document);
    const DependencyGraph& graph = documentGraph.graph;
    const DependencyGraph::Ordering ordering = graph.topologicalOrder();

    // Items that must not be used as inputs in this pass.
    std::set<ObjectId> broken;
    for (const std::vector<ObjectId>& cycle : ordering.cycles) {
        // Parameters depend only on parameters, so a cycle through a
        // parameter consists of parameters.
        std::vector<ParameterId> parameters;
        for (const ObjectId id : cycle) {
            if (const auto parameter = document.asParameter(id)) {
                parameters.push_back(*parameter);
            }
        }
        if (!parameters.empty()) {
            report.cycles.push_back(std::move(parameters));
        }
        broken.insert(cycle.begin(), cycle.end());
    }
    for (const ObjectId id : ordering.blocked) {
        if (const auto parameter = drivenParameter(document, id)) {
            report.blocked.push_back(*parameter);
        }
        broken.insert(id);
    }

    for (const ObjectId id : ordering.order) {
        const std::optional<ParameterId> parameter = drivenParameter(document, id);
        if (!parameter) {
            continue;
        }
        const bool inputBroken = std::ranges::any_of(graph.dependenciesOf(id),
                                                     [&](ObjectId input) { return broken.contains(input); });
        if (inputBroken) {
            report.blocked.push_back(*parameter);
            broken.insert(id);
            continue;
        }
        const auto value = evaluateParameterExpression(document, *parameter);
        if (!value) {
            report.failed.emplace(*parameter, value.error());
            broken.insert(id);
            continue;
        }
        const auto stored = ParameterExpressionWriter::store(document, *parameter, *value);
        if (!stored) {
            report.failed.emplace(*parameter, stored.error());
            broken.insert(id);
            continue;
        }
        report.evaluated.push_back(*parameter);
        if (*stored) {
            report.changed.push_back(*parameter);
        }
    }
    std::ranges::sort(report.blocked);
    return report;
}

} // namespace bettercad
