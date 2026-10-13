#include <bettercad/structural/StructuralData.hpp>

#include <cmath>
#include <format>
#include <string_view>

namespace bettercad::structural {

std::string_view toString(StructuralAnalysisMode mode) noexcept {
    switch (mode) {
    case StructuralAnalysisMode::LinearStatic:
        return "linear static";
    case StructuralAnalysisMode::LinearStaticWithGravity:
        return "linear static with gravity";
    }
    return "unknown";
}

std::string_view toString(DofComponent component) noexcept {
    switch (component) {
    case DofComponent::Ux:
        return "ux";
    case DofComponent::Uy:
        return "uy";
    case DofComponent::Uz:
        return "uz";
    }
    return "unknown";
}

std::string_view toString(TensorComponent component) noexcept {
    switch (component) {
    case TensorComponent::XX:
        return "xx";
    case TensorComponent::YY:
        return "yy";
    case TensorComponent::ZZ:
        return "zz";
    case TensorComponent::XY:
        return "xy";
    case TensorComponent::YZ:
        return "yz";
    case TensorComponent::ZX:
        return "zx";
    }
    return "unknown";
}

bool isFinite(const Strain6& strain) noexcept {
    return std::isfinite(strain.xx) && std::isfinite(strain.yy) && std::isfinite(strain.zz) &&
           std::isfinite(strain.gammaXy) && std::isfinite(strain.gammaYz) &&
           std::isfinite(strain.gammaZx);
}

bool isFinite(const Stress6& stress) noexcept {
    return bettercad::isFinite(stress.xx) && bettercad::isFinite(stress.yy) &&
           bettercad::isFinite(stress.zz) && bettercad::isFinite(stress.xy) &&
           bettercad::isFinite(stress.yz) && bettercad::isFinite(stress.zx);
}

// --------------------------------------------------------------------------
// Solver settings (ADR-047: canonical, so they live with the canonical data)
// --------------------------------------------------------------------------

std::string_view toString(SolverAlgorithm algorithm) noexcept {
    switch (algorithm) {
    case SolverAlgorithm::SimplicialLdlt:
        return "simplicial_ldlt";
    }
    return "unknown";
}

Result<void> validate(const SolverSettings& settings) {
    switch (settings.algorithm) {
    case SolverAlgorithm::SimplicialLdlt:
        break;
    default:
        return makeError(ErrorCode::InvalidArgument,
                         "the solver algorithm is not one this build recognises");
    }
    // A NaN threshold is refused here rather than left to decide something
    // inside a comparison: `r < NaN` is false, so it would reject every solve
    // for a reason no diagnostic could explain.
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
    return {};
}

} // namespace bettercad::structural
