#include <bettercad/sketch/SketchRegeneration.hpp>

#include <format>
#include <vector>

namespace bettercad::sketch {

Result<bool> applyDrivingParameters(Sketch& sketch, const ParameterTable& parameters) {
    std::vector<std::pair<ConstraintId, ParameterId>> driven;
    for (const Constraint& constraint : sketch.constraints()) {
        if (constraint.parameter) {
            driven.emplace_back(constraint.id, *constraint.parameter);
        }
    }
    bool changed = false;
    for (const auto& [constraintId, parameterId] : driven) {
        const Parameter* parameter = parameters.find(parameterId);
        if (parameter == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} is driven by {}, which does not exist", constraintId, parameterId));
        }
        auto value = parameter->as<Length>();
        if (!value) {
            return std::unexpected(value.error());
        }
        auto set = sketch.setConstraintValue(constraintId, *value);
        if (!set) {
            return std::unexpected(set.error());
        }
        changed |= *set;
    }
    return changed;
}

Result<SolveResult> regenerateSketch(Sketch& sketch, const ParameterTable& parameters,
                                     const SolverOptions& options) {
    const auto copy = sketch.clone();
    auto& working = static_cast<Sketch&>(*copy);
    if (auto applied = applyDrivingParameters(working, parameters); !applied) {
        return std::unexpected(applied.error());
    }
    SolveResult result = solve(working, options);
    if (result.solved()) {
        auto adopted = sketch.adoptSolution(working);
        if (!adopted) {
            return std::unexpected(adopted.error());
        }
        result.geometryChanged = *adopted;
    } else {
        result.geometryChanged = false;
    }
    return result;
}

} // namespace bettercad::sketch
