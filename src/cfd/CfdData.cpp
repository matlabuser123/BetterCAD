// The CFD analysis data model: validation of canonical records and the names of
// every enumerator (P19-DATA-001).

#include <bettercad/cfd/CfdData.hpp>

#include <cmath>
#include <format>

namespace bettercad::cfd {
namespace {

/// A ratio a setting may hold: finite and strictly positive.
[[nodiscard]] bool isPositiveRatio(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

std::string_view toString(CfdAnalysisMode mode) noexcept {
    switch (mode) {
    case CfdAnalysisMode::Steady:
        return "steady";
    case CfdAnalysisMode::Transient:
        return "transient";
    }
    return "unknown";
}

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

// --------------------------------------------------------------------------
// Boundary conditions
// --------------------------------------------------------------------------

std::string_view toString(CfdBoundaryConditionKind kind) noexcept {
    switch (kind) {
    case CfdBoundaryConditionKind::VelocityInlet:
        return "velocity_inlet";
    case CfdBoundaryConditionKind::PressureInlet:
        return "pressure_inlet";
    case CfdBoundaryConditionKind::PressureOutlet:
        return "pressure_outlet";
    case CfdBoundaryConditionKind::NoSlipWall:
        return "no_slip_wall";
    case CfdBoundaryConditionKind::MovingWall:
        return "moving_wall";
    case CfdBoundaryConditionKind::Slip:
        return "slip";
    }
    return "unknown";
}

CfdBoundaryConditionKind CfdBoundaryCondition::kind() const noexcept {
    // The variant's alternatives are declared in the enum's order, so the
    // index IS the kind. Asserted by `CfdData_TheKindIsTheAlternativeHeld`.
    return static_cast<CfdBoundaryConditionKind>(payload_.index());
}

const FaceName& CfdBoundaryCondition::face() const noexcept {
    return std::visit([](const auto& condition) -> const FaceName& { return condition.face; }, payload_);
}

bool CfdBoundaryCondition::fixesPressure() const noexcept {
    return pressureInlet() != nullptr || pressureOutlet() != nullptr;
}

Result<void> validate(const CfdBoundaryCondition& condition) {
    if (!condition.id().isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "a CFD boundary condition must carry a valid CfdBoundaryConditionId");
    }
    // CORE'S OWN SELECTOR CHECK, NOT A SECOND OPINION: the same one P16 applies
    // when the face is resolved, which is what makes this answerable without a
    // mesh.
    if (Result<void> selector = bettercad::validate(condition.face().face); !selector.has_value()) {
        return selector;
    }
    // TWO GAPS IN CORE'S CHECK, closed here because the fingerprint depends on
    // them: core does not range-check the role (every out-of-range value is named
    // "unknown", so two different ones would write the same text), and the face
    // name's feature may be the invalid id (P18 refuses that only in its commands).
    if (toString(condition.face().face.role) == "unknown") {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} names a face role this build does not recognise", condition.id()));
    }
    if (!condition.face().feature.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} names a face of the invalid feature id", condition.id()));
    }
    if (const VelocityInlet* inlet = condition.velocityInlet()) {
        if (!isFinite(inlet->normalSpeed) || inlet->normalSpeed.si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a velocity inlet with an inflow speed of {} m/s; it "
                                         "must be finite and positive (into the domain). A zero "
                                         "inflow is a no-slip wall",
                                         condition.id(), inlet->normalSpeed.si()));
        }
        return Result<void>{};
    }
    if (const PressureInlet* inlet = condition.pressureInlet()) {
        if (!isFinite(inlet->staticPressure)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a pressure inlet whose static pressure is not finite",
                                         condition.id()));
        }
        return Result<void>{};
    }
    if (const PressureOutlet* outlet = condition.pressureOutlet()) {
        if (!isFinite(outlet->staticPressure)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a pressure outlet whose static pressure is not finite",
                                         condition.id()));
        }
        return Result<void>{};
    }
    if (const MovingWall* wall = condition.movingWall()) {
        if (!isFinite(wall->velocity)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a moving wall whose velocity is not finite", condition.id()));
        }
        if (wall->velocity.magnitude().si() == 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a moving wall that does not move; a wall at rest is a "
                                         "no-slip wall",
                                         condition.id()));
        }
        return Result<void>{};
    }
    // A no-slip wall and a slip wall carry a face and nothing else.
    return Result<void>{};
}

// --------------------------------------------------------------------------
// Initial condition, pressure datum, gravity
// --------------------------------------------------------------------------

Result<void> validate(const CfdInitialCondition& initial) {
    if (!isFinite(initial.velocity)) {
        return makeError(ErrorCode::InvalidArgument, "the initial velocity is not finite");
    }
    if (!isFinite(initial.pressure)) {
        return makeError(ErrorCode::InvalidArgument, "the initial pressure is not finite");
    }
    return Result<void>{};
}

Result<void> validate(const CfdReferencePressure& reference) {
    if (!isFinite(reference.value)) {
        return makeError(ErrorCode::InvalidArgument, "the reference pressure is not finite");
    }
    if (!isFinite(reference.location)) {
        return makeError(ErrorCode::InvalidArgument, "the reference pressure's location is not finite");
    }
    return Result<void>{};
}

Result<void> validate(const CfdGravity& gravity) {
    if (!isFinite(gravity.acceleration)) {
        return makeError(ErrorCode::InvalidArgument, "the gravity vector is not finite");
    }
    if (gravity.acceleration.magnitude().si() == 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         "the gravity vector is zero; no gravity is the absence of a gravity record, "
                         "not a zero vector");
    }
    if (!isFinite(gravity.referenceHeight)) {
        return makeError(ErrorCode::InvalidArgument, "the gravity reference height is not finite");
    }
    return Result<void>{};
}

// --------------------------------------------------------------------------
// Solver settings
// --------------------------------------------------------------------------

Result<void> validate(const CfdIterationSettings& settings) {
    // NOT `!(x > 0 && x <= 1)` written as two comparisons that a NaN passes:
    // every comparison with NaN is false, so the finiteness check comes first.
    if (!isPositiveRatio(settings.velocityRelaxation) || settings.velocityRelaxation > 1.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the velocity under-relaxation must be in (0, 1], not {}",
                                     settings.velocityRelaxation));
    }
    if (!isPositiveRatio(settings.pressureRelaxation) || settings.pressureRelaxation > 1.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the pressure under-relaxation must be in (0, 1], not {}",
                                     settings.pressureRelaxation));
    }
    return Result<void>{};
}

Result<void> validate(const CfdConvergenceSettings& settings) {
    if (!isPositiveRatio(settings.momentumResidualTolerance)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the momentum residual tolerance must be finite and positive, not {}",
                                     settings.momentumResidualTolerance));
    }
    if (!isPositiveRatio(settings.continuityResidualTolerance)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the continuity residual tolerance must be finite and positive, not {}",
                                     settings.continuityResidualTolerance));
    }
    if (!std::isfinite(settings.divergenceFactor) || settings.divergenceFactor <= 1.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the divergence factor must be finite and greater than 1, not {}",
                                     settings.divergenceFactor));
    }
    return Result<void>{};
}

Result<void> validate(const CfdLinearSolverSettings& settings) {
    if (!isPositiveRatio(settings.relativeResidualTolerance)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the linear-solve residual tolerance must be finite and positive, not {}",
                                     settings.relativeResidualTolerance));
    }
    return Result<void>{};
}

Result<void> validate(const CfdSteadySettings& settings) {
    if (settings.maxIterations == 0) {
        return makeError(ErrorCode::InvalidArgument,
                         "a steady analysis needs at least one outer iteration");
    }
    return Result<void>{};
}

std::string_view toString(CfdTimeSampling sampling) noexcept {
    switch (sampling) {
    case CfdTimeSampling::FinalOnly:
        return "final_only";
    case CfdTimeSampling::EveryStep:
        return "every_step";
    }
    return "unknown";
}

Result<void> validate(const CfdTransientSettings& settings) {
    if (!isFinite(settings.timeStep) || settings.timeStep.si() <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the time step must be finite and positive, not {} s",
                                     settings.timeStep.si()));
    }
    if (!isFinite(settings.endTime) || settings.endTime.si() <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the end time must be finite and positive, not {} s", settings.endTime.si()));
    }
    if (settings.endTime < settings.timeStep) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the end time {} s is shorter than the time step {} s, so not one "
                                     "step could be taken",
                                     settings.endTime.si(), settings.timeStep.si()));
    }
    if (settings.maxIterationsPerStep == 0) {
        return makeError(ErrorCode::InvalidArgument,
                         "a transient analysis needs at least one outer iteration per step");
    }
    switch (settings.sampling) {
    case CfdTimeSampling::FinalOnly:
    case CfdTimeSampling::EveryStep:
        break;
    default:
        return makeError(ErrorCode::InvalidArgument, "the time sampling is not one this build recognises");
    }
    return Result<void>{};
}

std::string_view toString(CfdResidualHistory history) noexcept {
    switch (history) {
    case CfdResidualHistory::FinalOnly:
        return "final_only";
    case CfdResidualHistory::EveryIteration:
        return "every_iteration";
    }
    return "unknown";
}

Result<void> validate(const CfdOutputSettings& settings) {
    switch (settings.residualHistory) {
    case CfdResidualHistory::FinalOnly:
    case CfdResidualHistory::EveryIteration:
        return Result<void>{};
    }
    return makeError(ErrorCode::InvalidArgument, "the residual history policy is not one this build recognises");
}

// --------------------------------------------------------------------------
// Derived value types
// --------------------------------------------------------------------------

bool isWellFormed(const CfdResiduals& residuals) noexcept {
    for (const double value : {residuals.momentumX, residuals.momentumY, residuals.momentumZ, residuals.continuity}) {
        if (!std::isfinite(value) || value < 0.0) {
            return false;
        }
    }
    return true;
}

bool isWellFormed(const CfdMassBalance& balance) noexcept {
    return std::isfinite(balance.maxCellContinuity) && balance.maxCellContinuity >= 0.0 &&
           std::isfinite(balance.globalImbalance) && balance.globalImbalance >= 0.0;
}

} // namespace bettercad::cfd
