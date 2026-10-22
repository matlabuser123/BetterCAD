// The heat accounting of an accepted thermal solution (P18-BALANCE-001).
//
// NOTHING IN THIS FILE IS PHYSICS THAT ANOTHER MILESTONE OWNS. The load totals are
// P18-LOAD-001's contributions; the heat a convection face exchanges is P18-BC-001's
// `Tri3Convection::residualFrom` at the solved temperatures; `K`, `C` and `Q` are
// P18-ASSEMBLY-001's; the temperatures and the time grid are the solves'. What is written
// here is the residual `r = K T - Q` (or its backward-Euler form), the reading of its
// prescribed components as reactions, the stored energy `1^T C dT`, the sums, the order of the
// checks and the provenance.
//
// WHAT IS NOT HERE: no element integral, no `h A / 12`, no `q'' * area`, no Jacobian, no
// time integration, no remainder reaction (a reaction is never "what makes the balance
// close"), no `abs` on a heat rate, no unordered container, no thread, no clamp of a small
// term to zero.

#include <bettercad/thermal/ThermalBalance.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::thermal {
namespace {

[[nodiscard]] BalanceFailure failure(BalanceProblem problem, ErrorCode code, std::string message, std::size_t step = 0) {
    return BalanceFailure{.problem = problem, .error = makeError(code, std::move(message)).error(), .step = step};
}

[[nodiscard]] std::vector<double> siOf(std::span<const Temperature> temperatures) {
    std::vector<double> out;
    out.reserve(temperatures.size());
    for (const Temperature t : temperatures) {
        out.push_back(t.si());
    }
    return out;
}

[[nodiscard]] bool allFinite(std::span<const double> values) noexcept {
    return std::ranges::all_of(values, [](double v) { return std::isfinite(v); });
}

/// `sum_ij |A_ij| |x_j|`, W when `A` is in W/K and `x` in K: the magnitude the rounding of `A x`
/// scales with, in row order.
[[nodiscard]] double absoluteProduct(const ThermalSparseMatrix& matrix, std::span<const double> x) noexcept {
    const std::span<const ThermalSparseMatrix::Index> rowStart = matrix.rowStart();
    const std::span<const ThermalSparseMatrix::Index> inner = matrix.innerIndices();
    const std::span<const double> values = matrix.values();
    double sum = 0.0;
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        for (ThermalSparseMatrix::Index p = rowStart[row]; p < rowStart[row + 1]; ++p) {
            sum += std::abs(values[static_cast<std::size_t>(p)]) * std::abs(x[static_cast<std::size_t>(inner[static_cast<std::size_t>(p)])]);
        }
    }
    return sum;
}

[[nodiscard]] double absoluteSum(std::span<const double> values) noexcept {
    double sum = 0.0;
    for (const double v : values) {
        sum += std::abs(v);
    }
    return sum;
}

/// Sizes of the algebra against @p n unknowns.
[[nodiscard]] std::optional<BalanceFailure> checkSizes(const BalanceAlgebra& input, std::size_t n, std::size_t step) {
    if (input.conductance.rows() != n || input.conductance.columns() != n || input.heat.size() != n ||
        input.constraints.dofCount() != n) {
        return failure(BalanceProblem::NumberingMismatch, ErrorCode::InvalidArgument,
                       std::format("the conductance ({} x {}), the heat vector ({}) and the constraints ({}) do not "
                                   "describe {} unknowns",
                                   input.conductance.rows(), input.conductance.columns(), input.heat.size(),
                                   input.constraints.dofCount(), n),
                       step);
    }
    if (input.capacity != nullptr && (input.capacity->rows() != n || input.capacity->columns() != n)) {
        return failure(BalanceProblem::NumberingMismatch, ErrorCode::InvalidArgument,
                       std::format("the capacity ({} x {}) does not describe {} unknowns", input.capacity->rows(),
                                   input.capacity->columns(), n),
                       step);
    }
    return std::nullopt;
}

/// The attribution must account for the heat input: `sum Q = sum loads + sum Qconv`. A load or a
/// convection condition missing from, or extra to, the system breaks it, whatever the temperatures.
[[nodiscard]] std::optional<BalanceFailure> checkAttribution(const BalanceAlgebra& input, std::size_t step) {
    double attributed = 0.0;
    for (const ThermalLoadContribution& load : input.loads) {
        attributed += load.integrated.heatRate.si();
    }
    for (const PreparedConvectionBoundary& convection : input.convections) {
        attributed += convection.source().si();
    }
    const double total = [&] {
        double sum = 0.0;
        for (const double q : input.heat.values()) {
            sum += q;
        }
        return sum;
    }();
    const double scale = absoluteSum(input.heat.values()) + std::abs(attributed);
    if (!std::isfinite(attributed) || !std::isfinite(total) ||
        !balanceWithinTolerance(Power::fromSi(total - attributed), Power{}, Power::fromSi(scale))) {
        return failure(BalanceProblem::AttributionMissing, ErrorCode::FailedPrecondition,
                       std::format("the heat input of the system is {} W but the loads and convection conditions "
                                   "presented account for {} W: a load or a boundary is missing from, or extra to, the "
                                   "balance",
                                   total, attributed),
                       step);
    }
    return std::nullopt;
}

struct CategoryTotals {
    std::vector<LoadHeat> surface{};
    std::vector<LoadHeat> generation{};
    std::vector<ConvectionHeat> convections{};
    double surfaceInto = 0.0;
    double generationInto = 0.0;
    double convectionInto = 0.0;
};

[[nodiscard]] std::expected<CategoryTotals, BalanceFailure>
categoriesOf(const BalanceAlgebra& input, std::span<const Temperature> temperatures, std::size_t step) {
    CategoryTotals out;
    // Loads in the prepared loads' order; surface and generation kept apart, so each can be attributed.
    for (const ThermalLoadContribution& load : input.loads) {
        const LoadHeat entry{.load = load.load, .heatInto = load.integrated.heatRate};
        if (load.kind == ThermalLoadKind::SurfaceHeatFlux) {
            out.surface.push_back(entry);
            out.surfaceInto += entry.heatInto.si();
        } else {
            out.generation.push_back(entry);
            out.generationInto += entry.heatInto.si();
        }
    }
    // Convection conditions in the prepared boundary's order: overlapping conditions ADD.
    for (const PreparedConvectionBoundary& condition : input.convections) {
        std::expected<ConvectionHeat, BalanceFailure> exchanged = convectionHeatOf(condition, temperatures);
        if (!exchanged.has_value()) {
            exchanged.error().step = step;
            return std::unexpected(std::move(exchanged.error()));
        }
        out.convections.push_back(*exchanged);
        out.convectionInto += exchanged->heatInto.si();
    }
    if (!std::isfinite(out.surfaceInto) || !std::isfinite(out.generationInto) || !std::isfinite(out.convectionInto)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteCategory, ErrorCode::FailedPrecondition,
                                       "a category total (surface, generation or convection) is not finite", step));
    }
    return out;
}

/// The five terms, the residual and the status of one state, from its parts.
[[nodiscard]] std::expected<ThermalHeatBalance, BalanceFailure>
finish(CategoryTotals categories, std::vector<ReactionHeat> reactions, std::optional<Energy> stored, double timeStep,
       double arithmetic, std::size_t step) {
    ThermalHeatBalance out;
    double reactionInto = 0.0;
    for (const ReactionHeat& reaction : reactions) {
        reactionInto += reaction.heatInto.si();
    }
    if (!std::isfinite(reactionInto)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteReaction, ErrorCode::FailedPrecondition,
                                       "the total prescribed-temperature reaction is not finite", step));
    }
    out.terms.appliedSurfaceHeat = Power::fromSi(categories.surfaceInto);
    out.terms.generatedHeat = Power::fromSi(categories.generationInto);
    out.terms.convectionHeat = Power::fromSi(categories.convectionInto);
    out.terms.prescribedTemperatureReaction = Power::fromSi(reactionInto);
    if (stored.has_value()) {
        // dE / dt of the ACTUAL step.
        out.terms.storedEnergyRate = Power::fromSi(stored->si() / timeStep);
        out.storedEnergyChange = stored;
        out.stepSize = Time::fromSi(timeStep);
        out.step = step;
    }
    out.surfaceLoads = std::move(categories.surface);
    out.generationLoads = std::move(categories.generation);
    out.convections = std::move(categories.convections);
    out.reactions = std::move(reactions);
    out.arithmeticScale = Power::fromSi(arithmetic);
    const Power residual = out.residual();
    const Power gross = out.grossScale();
    if (!isFinite(out.terms) || !std::isfinite(residual.si()) || !std::isfinite(gross.si()) || !std::isfinite(arithmetic) ||
        !std::isfinite(out.normalizedResidual())) {
        return std::unexpected(failure(BalanceProblem::NonFiniteBalance, ErrorCode::FailedPrecondition,
                                       "the balance residual or one of its metrics is not finite", step));
    }
    out.status = balanceWithinTolerance(residual, gross, out.arithmeticScale) ? BalanceStatus::Balanced
                                                                              : BalanceStatus::OutOfTolerance;
    return out;
}

} // namespace

bool balanceWithinTolerance(Power residual, Power gross, Power arithmetic) noexcept {
    const double r = residual.si();
    const double g = gross.si();
    const double a = arithmetic.si();
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(a)) {
        return false;
    }
    return std::abs(r) <= kBalanceAbsoluteToleranceW + kBalanceRelativeTolerance * (std::abs(g) + std::abs(a));
}

std::string_view toString(BalanceProblem problem) noexcept {
    switch (problem) {
    case BalanceProblem::SourceMismatch:
        return "source_mismatch";
    case BalanceProblem::NumberingMismatch:
        return "numbering_mismatch";
    case BalanceProblem::ModeMismatch:
        return "mode_mismatch";
    case BalanceProblem::AttributionMissing:
        return "attribution_missing";
    case BalanceProblem::StepDataMissing:
        return "step_data_missing";
    case BalanceProblem::InvalidTimeStep:
        return "invalid_time_step";
    case BalanceProblem::NonFiniteResidual:
        return "non_finite_residual";
    case BalanceProblem::NonFiniteReaction:
        return "non_finite_reaction";
    case BalanceProblem::NonFiniteCategory:
        return "non_finite_category";
    case BalanceProblem::NonFiniteStorage:
        return "non_finite_storage";
    case BalanceProblem::NonFiniteBalance:
        return "non_finite_balance";
    }
    return "unknown";
}

std::string_view toString(BalanceStatus status) noexcept {
    switch (status) {
    case BalanceStatus::Balanced:
        return "balanced";
    case BalanceStatus::OutOfTolerance:
        return "out_of_tolerance";
    }
    return "unknown";
}

Power ThermalHeatBalance::grossScale() const noexcept {
    double sum = 0.0;
    for (const LoadHeat& load : surfaceLoads) {
        sum += std::abs(load.heatInto.si());
    }
    for (const LoadHeat& load : generationLoads) {
        sum += std::abs(load.heatInto.si());
    }
    for (const ConvectionHeat& condition : convections) {
        sum += std::abs(condition.heatInto.si());
    }
    for (const ReactionHeat& reaction : reactions) {
        sum += std::abs(reaction.heatInto.si());
    }
    if (terms.storedEnergyRate.has_value()) {
        sum += std::abs(terms.storedEnergyRate->si());
    }
    return Power::fromSi(sum);
}

double ThermalHeatBalance::normalizedResidual() const noexcept {
    const double gross = grossScale().si();
    return std::abs(residual().si()) / std::max(gross, kBalanceScaleFloorW);
}

// ---------------------------------------------------------------------------
// The algebra
// ---------------------------------------------------------------------------

std::expected<std::vector<double>, BalanceFailure>
steadyFullResidual(const ConductanceMatrix& conductance, const ThermalHeatVector& heat,
                   std::span<const Temperature> temperatures) {
    const std::size_t n = conductance.rows();
    if (conductance.columns() != n || heat.size() != n || temperatures.size() != n) {
        return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::InvalidArgument,
                                       std::format("K is {} x {}, Q has {} entries and T has {}: they do not describe "
                                                   "one set of unknowns",
                                                   conductance.rows(), conductance.columns(), heat.size(),
                                                   temperatures.size())));
    }
    const std::vector<double> t = siOf(temperatures);
    Result<std::vector<double>> kt = conductance.multiply(t);
    if (!kt.has_value()) {
        return std::unexpected(BalanceFailure{.problem = BalanceProblem::NumberingMismatch, .error = kt.error()});
    }
    std::vector<double> r(n);
    const std::span<const double> q = heat.values();
    for (std::size_t i = 0; i < n; ++i) {
        r[i] = (*kt)[i] - q[i];
    }
    if (!allFinite(r)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                                       "a component of the full residual K T - Q is not finite"));
    }
    return r;
}

std::expected<std::vector<double>, BalanceFailure>
transientFullResidual(const ConductanceMatrix& conductance, const CapacityMatrix& capacity,
                      const ThermalHeatVector& heat, std::span<const Temperature> previous,
                      std::span<const Temperature> next, double timeStep) {
    const std::size_t n = conductance.rows();
    if (!(timeStep > 0.0) || !std::isfinite(timeStep)) {
        return std::unexpected(failure(BalanceProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep)));
    }
    if (conductance.columns() != n || capacity.rows() != n || capacity.columns() != n || heat.size() != n ||
        previous.size() != n || next.size() != n) {
        return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::InvalidArgument,
                                       "K, C, Q and the two states do not describe one set of unknowns"));
    }
    const std::vector<double> after = siOf(next);
    std::vector<double> change(n);
    for (std::size_t i = 0; i < n; ++i) {
        change[i] = next[i].si() - previous[i].si();
    }
    Result<std::vector<double>> kt = conductance.multiply(after);
    Result<std::vector<double>> cdt = capacity.multiply(change);
    if (!kt.has_value()) {
        return std::unexpected(BalanceFailure{.problem = BalanceProblem::NumberingMismatch, .error = kt.error()});
    }
    if (!cdt.has_value()) {
        return std::unexpected(BalanceFailure{.problem = BalanceProblem::NumberingMismatch, .error = cdt.error()});
    }
    std::vector<double> r(n);
    const std::span<const double> q = heat.values();
    for (std::size_t i = 0; i < n; ++i) {
        r[i] = (*cdt)[i] / timeStep + (*kt)[i] - q[i];
    }
    if (!allFinite(r)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                                       "a component of the full residual C dT/dt + K T - Q is not finite"));
    }
    return r;
}

std::expected<Energy, BalanceFailure> storedEnergyChange(const CapacityMatrix& capacity,
                                                         std::span<const Temperature> previous,
                                                         std::span<const Temperature> next) {
    const std::size_t n = capacity.rows();
    if (capacity.columns() != n || previous.size() != n || next.size() != n) {
        return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::InvalidArgument,
                                       "C and the two states do not describe one set of unknowns"));
    }
    std::vector<double> change(n);
    for (std::size_t i = 0; i < n; ++i) {
        change[i] = next[i].si() - previous[i].si();
    }
    // The WHOLE matrix, prescribed unknowns included: the consistent C couples them to their neighbours.
    Result<std::vector<double>> cdt = capacity.multiply(change);
    if (!cdt.has_value()) {
        return std::unexpected(BalanceFailure{.problem = BalanceProblem::NumberingMismatch, .error = cdt.error()});
    }
    double sum = 0.0;
    for (const double row : *cdt) {
        sum += row;
    }
    if (!std::isfinite(sum)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteStorage, ErrorCode::FailedPrecondition,
                                       "the stored energy 1^T C dT is not finite"));
    }
    return Energy::fromSi(sum);
}

std::expected<ConvectionHeat, BalanceFailure> convectionHeatOf(const PreparedConvectionBoundary& condition,
                                                               std::span<const Temperature> temperatures) {
    double out = 0.0;
    for (const ConvectionFacet& facet : condition.facets) {
        Tri3Temperatures face{};
        for (std::size_t i = 0; i < kTri3Nodes; ++i) {
            const std::size_t dof = static_cast<std::size_t>(facet.dofs[i].value());
            if (dof >= temperatures.size()) {
                return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                                               std::format("convection facet {} names unknown {} of a numbering of {}",
                                                           facet.facet, dof, temperatures.size())));
            }
            face[i] = temperatures[dof];
        }
        // Kconv T - Qconv: outward positive. Never re-integrated from h and the ambient.
        const std::array<Power, kTri3Nodes> parts = facet.term.residualFrom(face);
        out += parts[0].si() + parts[1].si() + parts[2].si();
    }
    if (!std::isfinite(out)) {
        return std::unexpected(failure(BalanceProblem::NonFiniteCategory, ErrorCode::FailedPrecondition,
                                       std::format("the heat exchanged by convection condition {} is not finite",
                                                   condition.condition.value())));
    }
    return ConvectionHeat{.condition = condition.condition,
                          .area = condition.area(),
                          .heatOut = Power::fromSi(out),
                          .heatInto = Power::fromSi(-out)};
}

std::expected<std::vector<ReactionHeat>, BalanceFailure> reactionHeats(std::span<const double> fullResidual,
                                                                       const ThermalConstraintMap& constraints) {
    std::vector<ReactionHeat> out;
    out.reserve(constraints.size());
    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {
        const std::size_t dof = static_cast<std::size_t>(constraint.dof.value());
        if (dof >= fullResidual.size()) {
            return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                                           std::format("prescribed unknown {} is outside a residual of {}", dof,
                                                       fullResidual.size())));
        }
        // R_i = + r_i: the constraint supplies what the row's outflow exceeds the external sources by.
        const double heat = fullResidual[dof];
        if (!std::isfinite(heat)) {
            return std::unexpected(failure(BalanceProblem::NonFiniteReaction, ErrorCode::FailedPrecondition,
                                           std::format("the reaction of unknown {} is not finite", dof)));
        }
        out.push_back(ReactionHeat{.dof = constraint.dof, .heatInto = Power::fromSi(heat)});
    }
    return out;
}

std::expected<ThermalHeatBalance, BalanceFailure> steadyHeatBalance(const BalanceAlgebra& input,
                                                                    std::span<const Temperature> temperatures) {
    if (input.capacity != nullptr) {
        return std::unexpected(failure(BalanceProblem::ModeMismatch, ErrorCode::FailedPrecondition,
                                       "a steady balance was asked of an algebra that carries a capacity"));
    }
    const std::size_t n = input.conductance.rows();
    if (std::optional<BalanceFailure> bad = checkSizes(input, n, 0); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    if (temperatures.size() != n) {
        return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::InvalidArgument,
                                       std::format("{} temperatures cannot describe {} unknowns", temperatures.size(), n)));
    }
    if (std::optional<BalanceFailure> bad = checkAttribution(input, 0); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    std::expected<std::vector<double>, BalanceFailure> residual = steadyFullResidual(input.conductance, input.heat, temperatures);
    if (!residual.has_value()) {
        return std::unexpected(std::move(residual.error()));
    }
    std::expected<std::vector<ReactionHeat>, BalanceFailure> reactions = reactionHeats(*residual, input.constraints);
    if (!reactions.has_value()) {
        return std::unexpected(std::move(reactions.error()));
    }
    std::expected<CategoryTotals, BalanceFailure> categories = categoriesOf(input, temperatures, 0);
    if (!categories.has_value()) {
        return std::unexpected(std::move(categories.error()));
    }
    const std::vector<double> t = siOf(temperatures);
    const double arithmetic = absoluteProduct(input.conductance, t) + absoluteSum(input.heat.values());
    return finish(std::move(*categories), std::move(*reactions), std::nullopt, 1.0, arithmetic, 0);
}

std::expected<ThermalHeatBalance, BalanceFailure> transientStepHeatBalance(const BalanceAlgebra& input,
                                                                           std::span<const Temperature> previous,
                                                                           std::span<const Temperature> next,
                                                                           double timeStep, std::size_t step) {
    if (input.capacity == nullptr) {
        return std::unexpected(failure(BalanceProblem::ModeMismatch, ErrorCode::FailedPrecondition,
                                       "a transient balance was asked of an algebra with no capacity", step));
    }
    const std::size_t n = input.conductance.rows();
    if (std::optional<BalanceFailure> bad = checkSizes(input, n, step); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    if (previous.size() != n || next.size() != n) {
        return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::InvalidArgument,
                                       std::format("states of {} and {} temperatures cannot describe {} unknowns",
                                                   previous.size(), next.size(), n),
                                       step));
    }
    if (std::optional<BalanceFailure> bad = checkAttribution(input, step); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    // The FULL discrete residual: the storage term is part of the reaction of a prescribed unknown.
    std::expected<std::vector<double>, BalanceFailure> residual =
        transientFullResidual(input.conductance, *input.capacity, input.heat, previous, next, timeStep);
    if (!residual.has_value()) {
        residual.error().step = step;
        return std::unexpected(std::move(residual.error()));
    }
    std::expected<std::vector<ReactionHeat>, BalanceFailure> reactions = reactionHeats(*residual, input.constraints);
    if (!reactions.has_value()) {
        reactions.error().step = step;
        return std::unexpected(std::move(reactions.error()));
    }
    // The convection of a backward-Euler step is evaluated at T[n+1], the state its equation uses.
    std::expected<CategoryTotals, BalanceFailure> categories = categoriesOf(input, next, step);
    if (!categories.has_value()) {
        return std::unexpected(std::move(categories.error()));
    }
    std::expected<Energy, BalanceFailure> stored = storedEnergyChange(*input.capacity, previous, next);
    if (!stored.has_value()) {
        stored.error().step = step;
        return std::unexpected(std::move(stored.error()));
    }
    const std::vector<double> after = siOf(next);
    std::vector<double> change(n);
    for (std::size_t i = 0; i < n; ++i) {
        change[i] = next[i].si() - previous[i].si();
    }
    const double arithmetic = absoluteProduct(input.conductance, after) +
                              absoluteProduct(*input.capacity, change) / timeStep + absoluteSum(input.heat.values());
    return finish(std::move(*categories), std::move(*reactions), *stored, timeStep, arithmetic, step);
}

// ---------------------------------------------------------------------------
// The result
// ---------------------------------------------------------------------------

bool ThermalBalanceResult::balanced() const noexcept {
    for (const ThermalHeatBalance& state : states_) {
        if (state.status != BalanceStatus::Balanced) {
            return false;
        }
    }
    return true;
}

double ThermalBalanceResult::worstNormalizedResidual() const noexcept {
    double worst = 0.0;
    for (const ThermalHeatBalance& state : states_) {
        worst = std::max(worst, state.normalizedResidual());
    }
    return worst;
}

// ---------------------------------------------------------------------------
// The solution seams
// ---------------------------------------------------------------------------

namespace {

/// The checks the two seams share, before any arithmetic.
[[nodiscard]] std::optional<BalanceFailure> checkSeam(const ThermalSystem& system, const ThermalDofMap& map,
                                                      const ThermalConstraintMap& constraints,
                                                      std::span<const PreparedConvectionBoundary> convections,
                                                      ThermalAnalysisMode mode) {
    if (system.source().mode != mode) {
        return failure(BalanceProblem::ModeMismatch, ErrorCode::FailedPrecondition,
                       mode == ThermalAnalysisMode::Steady
                           ? "a steady balance was asked of a system assembled for a transient analysis"
                           : "a transient balance was asked of a system assembled for a steady analysis");
    }
    if (!system.describes(map)) {
        return failure(BalanceProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                       "the thermal numbering is not the one the system was assembled against");
    }
    if (!(constraints.stamp() == map.stamp()) || constraints.dofCount() != map.dofCount()) {
        return failure(BalanceProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                       "the constraint map does not belong to this numbering");
    }
    std::size_t facets = 0;
    for (const PreparedConvectionBoundary& condition : convections) {
        facets += condition.facets.size();
    }
    if (facets != system.convectionFacetCount()) {
        return failure(BalanceProblem::AttributionMissing, ErrorCode::FailedPrecondition,
                       std::format("the convection conditions presented hold {} facets but the system was assembled with {}",
                                   facets, system.convectionFacetCount()));
    }
    return std::nullopt;
}

/// Every prescribed unknown holds exactly the temperature the constraint map prescribes.
[[nodiscard]] std::optional<BalanceFailure> checkConstraintsHeld(const ThermalConstraintMap& constraints,
                                                                 std::span<const Temperature> state, std::size_t step) {
    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {
        const std::size_t dof = static_cast<std::size_t>(constraint.dof.value());
        if (dof >= state.size() || !(state[dof].si() == constraint.temperature.si())) {
            return failure(BalanceProblem::SourceMismatch, ErrorCode::FailedPrecondition,
                           std::format("unknown {} does not hold the temperature the constraint map prescribes: the "
                                       "solution and the boundary are not one problem",
                                       dof),
                           step);
        }
    }
    return std::nullopt;
}

} // namespace

std::expected<ThermalBalanceResult, BalanceFailure>
balanceSteadySolution(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                      std::span<const ThermalLoadContribution> loads,
                      std::span<const PreparedConvectionBoundary> convections, const SteadyThermalSolution& solution) {
    if (std::optional<BalanceFailure> bad = checkSeam(system, map, constraints, convections, ThermalAnalysisMode::Steady);
        bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    if (!(solution.source().system == system.source())) {
        return std::unexpected(failure(BalanceProblem::SourceMismatch, ErrorCode::FailedPrecondition,
                                       "the solution was not computed from this assembled system"));
    }
    if (!solution.describes(map) || solution.dofCount() != system.dofCount()) {
        return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                                       "the solution does not belong to this numbering"));
    }
    if (std::optional<BalanceFailure> bad = checkConstraintsHeld(constraints, solution.temperatures(), 0); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    const BalanceAlgebra input{.conductance = system.conductance(),
                               .capacity = nullptr,
                               .heat = system.heat(),
                               .constraints = constraints,
                               .loads = loads,
                               .convections = convections};
    std::expected<ThermalHeatBalance, BalanceFailure> state = steadyHeatBalance(input, solution.temperatures());
    if (!state.has_value()) {
        return std::unexpected(std::move(state.error()));
    }
    std::vector<ThermalHeatBalance> states;
    states.push_back(std::move(*state));
    // Published only here, through the one private constructor: a refused balance leaves nothing behind.
    return ThermalBalanceResult{postSourceOf(solution.source()), static_cast<std::size_t>(map.nodeCount()),
                                std::move(states), std::nullopt};
}

std::expected<ThermalBalanceResult, BalanceFailure>
balanceTransientSolution(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                         std::span<const ThermalLoadContribution> loads,
                         std::span<const PreparedConvectionBoundary> convections,
                         const TransientThermalSolution& solution) {
    if (std::optional<BalanceFailure> bad = checkSeam(system, map, constraints, convections, ThermalAnalysisMode::Transient);
        bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    if (!system.capacity().has_value()) {
        return std::unexpected(failure(BalanceProblem::ModeMismatch, ErrorCode::FailedPrecondition,
                                       "the system carries no capacity, so it has no stored energy"));
    }
    if (!(solution.source().system == system.source())) {
        return std::unexpected(failure(BalanceProblem::SourceMismatch, ErrorCode::FailedPrecondition,
                                       "the history was not computed from this assembled system"));
    }
    if (!solution.describes(map) || solution.dofCount() != system.dofCount()) {
        return std::unexpected(failure(BalanceProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                                       "the history does not belong to this numbering"));
    }
    const TimeGrid& grid = solution.grid();
    const std::size_t steps = grid.stepCount();
    if (steps == 0) {
        return std::unexpected(failure(BalanceProblem::StepDataMissing, ErrorCode::FailedPrecondition,
                                       "the history has no integration step, so there is nothing to balance"));
    }
    // THE STATES A STEP NEEDS: every sample of an EveryStep history, or the initial and final state of a
    // single step. Anything else would make an output interval look like an integration step.
    const bool everyStep = solution.sampleCount() == steps + 1;
    const bool singleStep = steps == 1 && solution.sampleCount() == 1;
    if (!everyStep && !singleStep) {
        return std::unexpected(failure(
            BalanceProblem::StepDataMissing, ErrorCode::FailedPrecondition,
            std::format("the history stored {} state(s) for {} integration steps: the states between the stored "
                        "ones are not available, and the interval between two stored states is not a backward-Euler "
                        "step; solve with the EveryStep output policy to balance each step",
                        solution.sampleCount(), steps)));
    }

    const auto stateAt = [&](std::size_t k) -> std::span<const Temperature> {
        if (everyStep) {
            return solution.temperaturesAt(k);
        }
        return k == 0 ? solution.initialTemperatures() : solution.temperaturesAt(0);
    };
    for (std::size_t k = 0; k <= steps; ++k) {
        if (std::optional<BalanceFailure> bad = checkConstraintsHeld(constraints, stateAt(k), k); bad.has_value()) {
            return std::unexpected(std::move(*bad));
        }
    }

    const BalanceAlgebra input{.conductance = system.conductance(),
                               .capacity = &*system.capacity(),
                               .heat = system.heat(),
                               .constraints = constraints,
                               .loads = loads,
                               .convections = convections};
    std::vector<ThermalHeatBalance> states;
    states.reserve(steps);
    double span = 0.0;
    double external = 0.0;
    double stored = 0.0;
    double allowed = 0.0;
    for (std::size_t k = 1; k <= steps; ++k) {
        const double dt = grid.stepAt(k - 1);
        std::expected<ThermalHeatBalance, BalanceFailure> state =
            transientStepHeatBalance(input, stateAt(k - 1), stateAt(k), dt, k);
        if (!state.has_value()) {
            return std::unexpected(std::move(state.error()));
        }
        state->time = Time::fromSi(grid.timeAt(k));
        const HeatBalance& t = state->terms;
        const double into = (((t.appliedSurfaceHeat + t.generatedHeat) + t.convectionHeat) + t.prescribedTemperatureReaction).si();
        span += dt;
        external += dt * into;
        stored += state->storedEnergyChange->si();
        allowed += dt * (kBalanceAbsoluteToleranceW +
                         kBalanceRelativeTolerance * (std::abs(state->grossScale().si()) + std::abs(state->arithmeticScale.si())));
        states.push_back(std::move(*state));
    }
    // The same energy from the two ends alone: an independent evaluation of the telescoping sum.
    std::expected<Energy, BalanceFailure> endpoints = storedEnergyChange(*input.capacity, stateAt(0), stateAt(steps));
    if (!endpoints.has_value()) {
        return std::unexpected(std::move(endpoints.error()));
    }
    const IntegratedEnergy integrated{.span = Time::fromSi(span),
                                      .external = Energy::fromSi(external),
                                      .stored = Energy::fromSi(stored),
                                      .endpoints = *endpoints,
                                      .residual = Energy::fromSi(external - stored),
                                      .allowed = Energy::fromSi(allowed)};
    if (!std::isfinite(integrated.external.si()) || !std::isfinite(integrated.stored.si()) ||
        !std::isfinite(integrated.residual.si())) {
        return std::unexpected(failure(BalanceProblem::NonFiniteBalance, ErrorCode::FailedPrecondition,
                                       "the integrated energy of the history is not finite"));
    }
    return ThermalBalanceResult{postSourceOf(solution.source()), static_cast<std::size_t>(map.nodeCount()),
                                std::move(states), integrated};
}

// ---------------------------------------------------------------------------
// The model-level entry points
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] BalanceFailure staleBalance(const Error& error) {
    return BalanceFailure{.problem = BalanceProblem::SourceMismatch, .error = error};
}

/// Checks 2-4: the system, the prepared loads and the prepared boundary are current, are this
/// numbering's, and the prepared loads are the ones the system was assembled from.
[[nodiscard]] std::optional<BalanceFailure>
checkInputs(const Document& document, const ThermalModel& model, const ThermalDofMap& map, const ThermalSystem& system,
            const PreparedThermalLoads& preparedLoads, const PreparedThermalBoundary& preparedBoundary,
            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
            ThermalAnalysisMode mode) {
    if (Result<void> current = requireCurrent(system, document, model, mode, loads, conditions); !current.has_value()) {
        return staleBalance(current.error());
    }
    if (Result<void> current = requireCurrent(preparedLoads, model, loads); !current.has_value()) {
        return staleBalance(current.error());
    }
    if (Result<void> current = requireCurrent(preparedBoundary, model, conditions); !current.has_value()) {
        return staleBalance(current.error());
    }
    if (!preparedLoads.describes(map) || !preparedBoundary.describes(map) || !system.describes(map)) {
        return failure(BalanceProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                       "the numbering presented is not the one the system and the prepared sets were built for");
    }
    if (!(preparedLoads.source().loads == system.source().loads)) {
        return failure(BalanceProblem::SourceMismatch, ErrorCode::FailedPrecondition,
                       "the prepared loads are not the loads the system was assembled from");
    }
    return std::nullopt;
}

[[nodiscard]] std::expected<ThermalBalanceResult, BalanceFailure>
runSteady(const Document& document, const ThermalModel& model, const ThermalDofMap& map, const ThermalSystem& system,
          const SteadyThermalSolution& solution, const PreparedThermalLoads& preparedLoads,
          const PreparedThermalBoundary& preparedBoundary, std::span<const ThermalLoad> loads,
          std::span<const ThermalBoundaryCondition> conditions, const ThermalSolverSettings& settings) {
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, settings); !current.has_value()) {
        return std::unexpected(staleBalance(current.error()));
    }
    if (std::optional<BalanceFailure> bad = checkInputs(document, model, map, system, preparedLoads, preparedBoundary, loads,
                                                        conditions, ThermalAnalysisMode::Steady);
        bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    return balanceSteadySolution(system, map, preparedBoundary.constraints(), preparedLoads.contributions(),
                                 preparedBoundary.convections(), solution);
}

[[nodiscard]] std::expected<ThermalBalanceResult, BalanceFailure>
runTransient(const Document& document, const ThermalModel& model, const ThermalDofMap& map, const ThermalSystem& system,
             const TransientThermalSolution& solution, const PreparedThermalLoads& preparedLoads,
             const PreparedThermalBoundary& preparedBoundary, std::span<const ThermalLoad> loads,
             std::span<const ThermalBoundaryCondition> conditions, const UniformInitialTemperature& initial,
             const TransientSettings& transient, const ThermalSolverSettings& settings) {
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, initial, transient, settings);
        !current.has_value()) {
        return std::unexpected(staleBalance(current.error()));
    }
    if (std::optional<BalanceFailure> bad = checkInputs(document, model, map, system, preparedLoads, preparedBoundary, loads,
                                                        conditions, ThermalAnalysisMode::Transient);
        bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    return balanceTransientSolution(system, map, preparedBoundary.constraints(), preparedLoads.contributions(),
                                    preparedBoundary.convections(), solution);
}

} // namespace

Result<ThermalBalanceResult> recoverSteadyBalance(const Document& document, const ThermalModel& model,
                                                  const ThermalDofMap& map, const ThermalSystem& system,
                                                  const SteadyThermalSolution& solution,
                                                  const PreparedThermalLoads& preparedLoads,
                                                  const PreparedThermalBoundary& preparedBoundary,
                                                  std::span<const ThermalLoad> loads,
                                                  std::span<const ThermalBoundaryCondition> conditions,
                                                  const ThermalSolverSettings& settings) {
    std::expected<ThermalBalanceResult, BalanceFailure> balanced =
        runSteady(document, model, map, system, solution, preparedLoads, preparedBoundary, loads, conditions, settings);
    if (!balanced.has_value()) {
        return std::unexpected(std::move(balanced.error().error));
    }
    return std::move(*balanced);
}

Result<ThermalBalanceResult> recoverTransientBalance(const Document& document, const ThermalModel& model,
                                                     const ThermalDofMap& map, const ThermalSystem& system,
                                                     const TransientThermalSolution& solution,
                                                     const PreparedThermalLoads& preparedLoads,
                                                     const PreparedThermalBoundary& preparedBoundary,
                                                     std::span<const ThermalLoad> loads,
                                                     std::span<const ThermalBoundaryCondition> conditions,
                                                     const UniformInitialTemperature& initial,
                                                     const TransientSettings& transient,
                                                     const ThermalSolverSettings& settings) {
    std::expected<ThermalBalanceResult, BalanceFailure> balanced = runTransient(
        document, model, map, system, solution, preparedLoads, preparedBoundary, loads, conditions, initial, transient, settings);
    if (!balanced.has_value()) {
        return std::unexpected(std::move(balanced.error().error));
    }
    return std::move(*balanced);
}

std::optional<BalanceFailure> steadyBalanceFailure(const Document& document, const ThermalModel& model,
                                                   const ThermalDofMap& map, const ThermalSystem& system,
                                                   const SteadyThermalSolution& solution,
                                                   const PreparedThermalLoads& preparedLoads,
                                                   const PreparedThermalBoundary& preparedBoundary,
                                                   std::span<const ThermalLoad> loads,
                                                   std::span<const ThermalBoundaryCondition> conditions,
                                                   const ThermalSolverSettings& settings) {
    std::expected<ThermalBalanceResult, BalanceFailure> balanced =
        runSteady(document, model, map, system, solution, preparedLoads, preparedBoundary, loads, conditions, settings);
    if (balanced.has_value()) {
        return std::nullopt;
    }
    return std::move(balanced.error());
}

std::optional<BalanceFailure> transientBalanceFailure(const Document& document, const ThermalModel& model,
                                                      const ThermalDofMap& map, const ThermalSystem& system,
                                                      const TransientThermalSolution& solution,
                                                      const PreparedThermalLoads& preparedLoads,
                                                      const PreparedThermalBoundary& preparedBoundary,
                                                      std::span<const ThermalLoad> loads,
                                                      std::span<const ThermalBoundaryCondition> conditions,
                                                      const UniformInitialTemperature& initial,
                                                      const TransientSettings& transient,
                                                      const ThermalSolverSettings& settings) {
    std::expected<ThermalBalanceResult, BalanceFailure> balanced = runTransient(
        document, model, map, system, solution, preparedLoads, preparedBoundary, loads, conditions, initial, transient, settings);
    if (balanced.has_value()) {
        return std::nullopt;
    }
    return std::move(balanced.error());
}

// ---------------------------------------------------------------------------
// Currentness of a held balance
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::string reasonsText(const std::vector<TransientStaleReason>& reasons) {
    std::string names;
    for (const TransientStaleReason reason : reasons) {
        if (!names.empty()) {
            names += ", ";
        }
        names += toString(reason);
    }
    return names;
}

} // namespace

Result<void> requireCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                            const ThermalSolverSettings& settings) {
    if (result.mode() != ThermalAnalysisMode::Steady) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this balance was taken from a transient history; ask with the transient settings");
    }
    const Result<SteadySolutionSource> current = currentSteadySolutionSource(document, model, loads, conditions, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the balance cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the balance no longer describes the model: {} changed. Solve and balance again",
                                 reasonsText(reasons)));
}

Result<void> requireCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                            const UniformInitialTemperature& initial, const TransientSettings& transient,
                            const ThermalSolverSettings& settings) {
    if (result.mode() != ThermalAnalysisMode::Transient) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this balance was taken from a steady solution; ask without the transient settings");
    }
    const Result<TransientSolutionSource> current =
        currentTransientSolutionSource(document, model, loads, conditions, initial, transient, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the balance cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the balance no longer describes the model: {} changed. Solve and balance again",
                                 reasonsText(reasons)));
}

bool isCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const ThermalSolverSettings& settings) {
    return requireCurrent(result, document, model, loads, conditions, settings).has_value();
}

bool isCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const UniformInitialTemperature& initial, const TransientSettings& transient,
               const ThermalSolverSettings& settings) {
    return requireCurrent(result, document, model, loads, conditions, initial, transient, settings).has_value();
}

} // namespace bettercad::thermal
