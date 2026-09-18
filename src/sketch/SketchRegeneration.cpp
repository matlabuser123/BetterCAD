#include <bettercad/sketch/SketchRegeneration.hpp>

#include <format>
#include <vector>

namespace bettercad::sketch {

Result<bool> applyDrivingParameters(Sketch& sketch, const ParameterTable& parameters,
                                    const ParameterOverrides& overrides) {
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
        // The value in force: the active configuration's override, if it
        // has one, else the parameter's own. The dimension is the
        // parameter's either way, since Document only accepts an override of
        // the parameter's dimension.
        const auto found = overrides.find(item.parameter);
        const double si = found != overrides.end() ? found->second.siValue : parameter->siValue();
        Result<bool> set = false;
        if (item.angle) {
            auto value = parameter->as<Angle>();
            if (!value) {
                return std::unexpected(value.error());
            }
            set = sketch.setConstraintAngle(item.constraint, Angle::fromSi(si));
        } else {
            auto value = parameter->as<Length>();
            if (!value) {
                return std::unexpected(value.error());
            }
            set = sketch.setConstraintValue(item.constraint, Length::fromSi(si));
        }
        if (!set) {
            return std::unexpected(set.error());
        }
        changed |= *set;
    }
    return changed;
}

Result<SolveResult> regenerateSketch(Sketch& sketch, const ParameterTable& parameters,
                                     const SolverOptions& options, const ParameterOverrides& overrides) {
    const auto copy = sketch.clone();
    auto& working = static_cast<Sketch&>(*copy);
    if (auto applied = applyDrivingParameters(working, parameters, overrides); !applied) {
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
