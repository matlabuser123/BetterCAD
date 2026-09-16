#include <bettercad/sketch/SketchRegeneration.hpp>

#include <format>
#include <vector>

namespace bettercad::sketch {

Result<bool> applyDrivingParameters(Sketch& sketch, const ParameterTable& parameters) {
    struct Driven {
        ConstraintId constraint;
        ParameterId parameter;
        bool angle = false;
    };
    std::vector<Driven> driven;
    for (const Constraint& constraint : sketch.constraints()) {
        if (constraint.parameter) {
            driven.push_back({constraint.id, *constraint.parameter, hasAngle(constraint.type)});
        }
    }
    bool changed = false;
    for (const Driven& item : driven) {
        const Parameter* parameter = parameters.find(item.parameter);
        if (parameter == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("{} is driven by {}, which does not exist",
                                                              item.constraint, item.parameter));
        }
        Result<bool> set = false;
        if (item.angle) {
            auto value = parameter->as<Angle>();
            if (!value) {
                return std::unexpected(value.error());
            }
            set = sketch.setConstraintAngle(item.constraint, *value);
        } else {
            auto value = parameter->as<Length>();
            if (!value) {
                return std::unexpected(value.error());
            }
            set = sketch.setConstraintValue(item.constraint, *value);
        }
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
