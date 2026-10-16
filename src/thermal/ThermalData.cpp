// The thermal analysis data model: validation of canonical records and the
// names of every enumerator (P18-DATA-001).

#include <bettercad/thermal/ThermalData.hpp>

#include <cmath>
#include <format>

namespace bettercad::thermal {

std::string_view toString(ThermalAnalysisMode mode) noexcept {
    switch (mode) {
    case ThermalAnalysisMode::Steady:
        return "steady";
    case ThermalAnalysisMode::Transient:
        return "transient";
    }
    return "unknown";
}

// --------------------------------------------------------------------------
// Loads
// --------------------------------------------------------------------------

std::string_view toString(ThermalLoadKind kind) noexcept {
    switch (kind) {
    case ThermalLoadKind::SurfaceHeatFlux:
        return "surface_heat_flux";
    case ThermalLoadKind::VolumetricHeatSource:
        return "volumetric_heat_source";
    }
    return "unknown";
}

ThermalLoadKind ThermalLoad::kind() const noexcept {
    return std::holds_alternative<SurfaceHeatFluxLoad>(payload_) ? ThermalLoadKind::SurfaceHeatFlux
                                                                 : ThermalLoadKind::VolumetricHeatSource;
}

std::optional<FaceName> ThermalLoad::target() const noexcept {
    if (const SurfaceHeatFluxLoad* flux = surfaceHeatFlux()) {
        return flux->face;
    }
    return std::nullopt;
}

Result<void> validate(const ThermalLoad& load) {
    if (!load.id().isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "a thermal load must carry a valid ThermalLoadId");
    }
    if (const SurfaceHeatFluxLoad* flux = load.surfaceHeatFlux()) {
        // CORE'S OWN SELECTOR CHECK, NOT A SECOND OPINION. P16's
        // `boundaryFacetsOf` applies the same one when the load is resolved;
        // asking it here is what makes `validate` answerable without a mesh.
        if (Result<void> selector = bettercad::validate(flux->face.face); !selector.has_value()) {
            return selector;
        }
        if (!isFinite(flux->flux)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} carries a heat flux that is not finite", load.id()));
        }
        return Result<void>{};
    }
    const VolumetricHeatSourceLoad* source = load.volumetricHeatSource();
    if (source == nullptr || !isFinite(source->generation)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} carries a heat generation that is not finite", load.id()));
    }
    return Result<void>{};
}

// --------------------------------------------------------------------------
// Boundary conditions
// --------------------------------------------------------------------------

std::string_view toString(ThermalBoundaryConditionKind kind) noexcept {
    switch (kind) {
    case ThermalBoundaryConditionKind::PrescribedTemperature:
        return "prescribed_temperature";
    case ThermalBoundaryConditionKind::Convection:
        return "convection";
    }
    return "unknown";
}

ThermalBoundaryConditionKind ThermalBoundaryCondition::kind() const noexcept {
    return std::holds_alternative<PrescribedTemperature>(payload_)
               ? ThermalBoundaryConditionKind::PrescribedTemperature
               : ThermalBoundaryConditionKind::Convection;
}

const FaceName& ThermalBoundaryCondition::face() const noexcept {
    if (const PrescribedTemperature* held = prescribedTemperature()) {
        return held->face;
    }
    return std::get<ConvectionBoundary>(payload_).face;
}

Result<void> validate(const ThermalBoundaryCondition& condition) {
    if (!condition.id().isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "a thermal boundary condition must carry a valid "
                         "ThermalBoundaryConditionId");
    }
    if (Result<void> selector = bettercad::validate(condition.face().face); !selector.has_value()) {
        return selector;
    }
    if (const PrescribedTemperature* held = condition.prescribedTemperature()) {
        if (!isAbsoluteTemperature(held->value)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} prescribes {} K, which is not a finite absolute "
                                         "temperature",
                                         condition.id(), held->value.si()));
        }
        return Result<void>{};
    }
    const ConvectionBoundary& convection = *condition.convection();
    if (!isFinite(convection.coefficient) || convection.coefficient.si() <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} has a convection coefficient of {} W/(m^2 K); it must "
                                     "be finite and positive, or the face exchanges nothing",
                                     condition.id(), convection.coefficient.si()));
    }
    if (!isAbsoluteTemperature(convection.ambient)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} has an ambient of {} K, which is not a finite absolute "
                                     "temperature",
                                     condition.id(), convection.ambient.si()));
    }
    return Result<void>{};
}

// --------------------------------------------------------------------------
// Initial condition
// --------------------------------------------------------------------------

Result<void> validate(const UniformInitialTemperature& initial) {
    if (!isAbsoluteTemperature(initial.value)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the initial temperature {} K is not a finite absolute "
                                     "temperature",
                                     initial.value.si()));
    }
    return Result<void>{};
}

// --------------------------------------------------------------------------
// Solver and transient settings
// --------------------------------------------------------------------------

Result<void> validate(const ThermalSolverSettings& settings) {
    if (!std::isfinite(settings.relativeResidualTolerance) ||
        settings.relativeResidualTolerance <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the relative residual tolerance must be finite and "
                                     "positive, not {}",
                                     settings.relativeResidualTolerance));
    }
    if (!std::isfinite(settings.pivotFloor) || settings.pivotFloor <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the pivot floor must be finite and positive, not {}",
                                     settings.pivotFloor));
    }
    return Result<void>{};
}

std::string_view toString(TransientOutputPolicy policy) noexcept {
    switch (policy) {
    case TransientOutputPolicy::FinalOnly:
        return "final_only";
    case TransientOutputPolicy::EveryStep:
        return "every_step";
    }
    return "unknown";
}

Result<void> validate(const TransientSettings& settings) {
    if (!isFinite(settings.timeStep) || settings.timeStep.si() <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the time step must be finite and positive, not {} s",
                                     settings.timeStep.si()));
    }
    if (!isFinite(settings.endTime) || settings.endTime.si() <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the end time must be finite and positive, not {} s",
                                     settings.endTime.si()));
    }
    if (settings.endTime < settings.timeStep) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the end time {} s is shorter than the time step {} s, so "
                                     "not one step could be taken",
                                     settings.endTime.si(), settings.timeStep.si()));
    }
    switch (settings.output) {
    case TransientOutputPolicy::FinalOnly:
    case TransientOutputPolicy::EveryStep:
        break;
    default:
        return makeError(ErrorCode::InvalidArgument,
                         "the transient output policy is not one this build recognises");
    }
    return Result<void>{};
}

// --------------------------------------------------------------------------
// Derived value types
// --------------------------------------------------------------------------

std::string_view toString(VectorComponent component) noexcept {
    switch (component) {
    case VectorComponent::X:
        return "x";
    case VectorComponent::Y:
        return "y";
    case VectorComponent::Z:
        return "z";
    }
    return "unknown";
}

bool isFinite(const TemperatureGradient3& gradient) noexcept {
    return bettercad::isFinite(gradient.x) && bettercad::isFinite(gradient.y) &&
           bettercad::isFinite(gradient.z);
}

bool isFinite(const HeatFlux3& flux) noexcept {
    return bettercad::isFinite(flux.x) && bettercad::isFinite(flux.y) && bettercad::isFinite(flux.z);
}

bool isFinite(const HeatBalance& balance) noexcept {
    return bettercad::isFinite(balance.appliedSurfaceHeat) &&
           bettercad::isFinite(balance.generatedHeat) &&
           bettercad::isFinite(balance.convectionHeat) &&
           bettercad::isFinite(balance.prescribedTemperatureReaction) &&
           (!balance.storedEnergyRate.has_value() || bettercad::isFinite(*balance.storedEnergyRate));
}

} // namespace bettercad::thermal
