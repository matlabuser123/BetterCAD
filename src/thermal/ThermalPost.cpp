// The recovery of the derived thermal fields (P18-POST-001).
//
// THE GEOMETRY IS NOT HERE. `grad(T) = G Te` is `Tet4Geometry::gradientFrom`
// (P18-ELEM-001); the gather of `Te` is `ThermalDofMap::elementDofs` (P18-DOF-001);
// the solved temperatures are the solver's. What this file adds is the Fourier law
// `q = -k grad(T)`, the Euclidean magnitude, the finiteness gates, the order of the
// checks and the provenance. No Jacobian, no shape function, no determinant and no
// matrix product is written in this file.
//
// WHAT IS NOT HERE EITHER: no nodal averaging, no smoothing, no boundary normal, no
// convection or generation or storage term in q, no Celsius, no unit conversion, no
// colour, no unordered container, no thread.

#include <bettercad/thermal/ThermalPost.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::thermal {
namespace {

[[nodiscard]] PostFailure failure(PostProblem problem, ErrorCode code, std::string message,
                                  std::optional<meshing::ElementId> element = std::nullopt, std::size_t state = 0) {
    return PostFailure{.problem = problem,
                       .error = makeError(code, std::move(message)).error(),
                       .element = element,
                       .state = state};
}

/// The mesh-dependent, temperature-independent part of the recovery: for each
/// tetrahedron, in mesh order, its unknowns in its OWN node order and its geometry.
///
/// Built once per mesh and used for every state of a transient history, so the
/// Jacobian of an element is formed once however many time levels are recovered.
struct ElementTable {
    std::vector<meshing::ElementId> ids{};
    std::vector<std::array<ThermalDofIndex, 4>> dofs{};
    std::vector<Tet4Geometry> geometries{};
};

[[nodiscard]] std::expected<ElementTable, PostFailure> buildElementTable(const meshing::Mesh& mesh,
                                                                         const ThermalDofMap& map) {
    ElementTable table;
    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
    table.ids.reserve(tets.size());
    table.dofs.reserve(tets.size());
    table.geometries.reserve(tets.size());
    for (const meshing::Tetrahedron& tet : tets) {
        const Result<std::array<ThermalDofIndex, 4>> dofs = map.elementDofs(tet);
        if (!dofs.has_value()) {
            return std::unexpected(failure(PostProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                                           std::format("element {}: {}", tet.id, dofs.error().message), tet.id));
        }
        const Result<Tet4Corners> corners = cornersOf(mesh, tet);
        if (!corners.has_value()) {
            return std::unexpected(failure(PostProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                                           std::format("element {}: {}", tet.id, corners.error().message), tet.id));
        }
        // REFUSED, NEVER SKIPPED: an element that is not valid is a hole in the field.
        const Result<Tet4Geometry> geometry = computeTet4Geometry(*corners);
        if (!geometry.has_value()) {
            return std::unexpected(failure(PostProblem::InvalidTetGeometry, ErrorCode::FailedPrecondition,
                                           std::format("element {}: {}", tet.id, geometry.error().message), tet.id));
        }
        table.ids.push_back(tet.id);
        table.dofs.push_back(*dofs);
        table.geometries.push_back(*geometry);
    }
    return table;
}

[[nodiscard]] std::optional<PostFailure> checkConductivity(ThermalConductivity conductivity) {
    if (!std::isfinite(conductivity.si()) || !(conductivity.si() > 0.0)) {
        return failure(PostProblem::ThermalConductivityInvalid, ErrorCode::InvalidArgument,
                       std::format("the thermal conductivity must be finite and positive, got {} W/(m K)",
                                   conductivity.si()));
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PostFailure> checkNumbering(const meshing::Mesh& mesh, const ThermalDofMap& map) {
    if (!map.describes(mesh)) {
        return failure(PostProblem::NumberingMismatch, ErrorCode::FailedPrecondition,
                       "the thermal numbering is not the one of this mesh, so its indices are not this mesh's nodes");
    }
    return std::nullopt;
}

/// Cardinality, finiteness and the absolute-temperature rule, before any element.
[[nodiscard]] std::optional<PostFailure> checkTemperatures(const ThermalDofMap& map,
                                                           std::span<const Temperature> temperatures,
                                                           std::size_t state) {
    if (temperatures.size() != static_cast<std::size_t>(map.dofCount())) {
        return failure(PostProblem::TemperatureCardinalityMismatch, ErrorCode::InvalidArgument,
                       std::format("{} temperatures cannot describe the numbering's {} unknowns", temperatures.size(),
                                   map.dofCount()),
                       std::nullopt, state);
    }
    for (std::size_t i = 0; i < temperatures.size(); ++i) {
        if (!std::isfinite(temperatures[i].si())) {
            return failure(PostProblem::NonFiniteTemperatureResult, ErrorCode::FailedPrecondition,
                           std::format("the temperature of unknown {} is not finite", i), std::nullopt, state);
        }
        if (!isAbsoluteTemperature(temperatures[i])) {
            return failure(PostProblem::TemperatureBelowAbsoluteZero, ErrorCode::FailedPrecondition,
                           std::format("the temperature of unknown {} ({} K) is below absolute zero", i,
                                       temperatures[i].si()),
                           std::nullopt, state);
        }
    }
    return std::nullopt;
}

/// The Fourier law and the gates, for one state, over a prepared element table.
[[nodiscard]] std::expected<ThermalElementFields, PostFailure>
fieldsFrom(const ElementTable& table, ThermalConductivity conductivity, std::span<const Temperature> temperatures,
           std::size_t state) {
    ThermalElementFields out;
    const std::size_t count = table.ids.size();
    out.gradients.reserve(count);
    out.heatFluxes.reserve(count);
    out.heatFluxMagnitudes.reserve(count);
    for (std::size_t e = 0; e < count; ++e) {
        // Te in the element's OWN node order: not sorted, not reordered.
        Tet4Temperatures local;
        for (std::size_t i = 0; i < kTet4Nodes; ++i) {
            local[i] = temperatures[static_cast<std::size_t>(table.dofs[e][i].value())];
        }
        // grad(T) = G Te -- P18-ELEM's path, not rewritten here.
        const TemperatureGradient3 gradient = table.geometries[e].gradientFrom(local);
        if (!isFinite(gradient)) {
            return std::unexpected(failure(PostProblem::NonFiniteTemperatureGradient, ErrorCode::FailedPrecondition,
                                           std::format("element {}: the temperature gradient is not finite",
                                                       table.ids[e]),
                                           table.ids[e], state));
        }
        // q = -k grad(T): the minus sign is the physics (heat flows toward lower temperature). It is written
        // as `0 - k g`, which equals `-(k g)` for every nonzero value and gives +0 rather than -0 for a zero
        // gradient, so a constant field has a flux of zero and not of negative zero.
        const HeatFlux3 flux{.x = HeatFlux{} - conductivity * gradient.x,
                             .y = HeatFlux{} - conductivity * gradient.y,
                             .z = HeatFlux{} - conductivity * gradient.z};
        if (!isFinite(flux)) {
            return std::unexpected(failure(PostProblem::NonFiniteHeatFlux, ErrorCode::FailedPrecondition,
                                           std::format("element {}: the heat flux is not finite (k grad(T) "
                                                       "overflowed)",
                                                       table.ids[e]),
                                           table.ids[e], state));
        }
        // |q| from the components (std::hypot: no intermediate overflow). Derived from q, never stored alone.
        const HeatFlux magnitude = flux.magnitude();
        if (!std::isfinite(magnitude.si())) {
            return std::unexpected(failure(PostProblem::NonFiniteHeatFluxMagnitude, ErrorCode::FailedPrecondition,
                                           std::format("element {}: the heat-flux magnitude is not finite",
                                                       table.ids[e]),
                                           table.ids[e], state));
        }
        out.gradients.push_back(gradient);
        out.heatFluxes.push_back(flux);
        out.heatFluxMagnitudes.push_back(magnitude);
    }
    return out;
}

} // namespace

std::string_view toString(ThermalPostField field) noexcept {
    switch (field) {
    case ThermalPostField::Temperature:
        return "temperature";
    case ThermalPostField::TemperatureGradient:
        return "temperature_gradient";
    case ThermalPostField::HeatFlux:
        return "heat_flux";
    case ThermalPostField::HeatFluxMagnitude:
        return "heat_flux_magnitude";
    }
    return "unknown";
}

std::string_view toString(FieldLocation location) noexcept {
    switch (location) {
    case FieldLocation::Node:
        return "node";
    case FieldLocation::Element:
        return "element";
    }
    return "unknown";
}

ThermalFieldDescriptor descriptorOf(ThermalPostField field) noexcept {
    switch (field) {
    case ThermalPostField::Temperature:
        return ThermalFieldDescriptor{.field = field,
                                      .location = FieldLocation::Node,
                                      .componentCount = 1,
                                      .componentNames = {"T", "", ""},
                                      .dimension = Temperature::dimension,
                                      .unit = "K"};
    case ThermalPostField::TemperatureGradient:
        return ThermalFieldDescriptor{.field = field,
                                      .location = FieldLocation::Element,
                                      .componentCount = 3,
                                      .componentNames = {"dT/dx", "dT/dy", "dT/dz"},
                                      .dimension = TemperatureGradient::dimension,
                                      .unit = "K/m"};
    case ThermalPostField::HeatFlux:
        return ThermalFieldDescriptor{.field = field,
                                      .location = FieldLocation::Element,
                                      .componentCount = 3,
                                      .componentNames = {"qx", "qy", "qz"},
                                      .dimension = HeatFlux::dimension,
                                      .unit = "W/m^2"};
    case ThermalPostField::HeatFluxMagnitude:
        return ThermalFieldDescriptor{.field = field,
                                      .location = FieldLocation::Element,
                                      .componentCount = 1,
                                      .componentNames = {"|q|", "", ""},
                                      .dimension = HeatFlux::dimension,
                                      .unit = "W/m^2"};
    }
    return {};
}

std::string_view toString(PostProblem problem) noexcept {
    switch (problem) {
    case PostProblem::ThermalConductivityInvalid:
        return "thermal_conductivity_invalid";
    case PostProblem::PostSourceMismatch:
        return "post_source_mismatch";
    case PostProblem::NumberingMismatch:
        return "numbering_mismatch";
    case PostProblem::MaterialMismatch:
        return "material_mismatch";
    case PostProblem::TemperatureCardinalityMismatch:
        return "temperature_cardinality_mismatch";
    case PostProblem::NonFiniteTemperatureResult:
        return "non_finite_temperature_result";
    case PostProblem::TemperatureBelowAbsoluteZero:
        return "temperature_below_absolute_zero";
    case PostProblem::InvalidTetGeometry:
        return "invalid_tet_geometry";
    case PostProblem::NonFiniteTemperatureGradient:
        return "non_finite_temperature_gradient";
    case PostProblem::NonFiniteHeatFlux:
        return "non_finite_heat_flux";
    case PostProblem::NonFiniteHeatFluxMagnitude:
        return "non_finite_heat_flux_magnitude";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The element kernel
// ---------------------------------------------------------------------------

std::expected<ThermalElementFields, PostFailure>
recoverThermalElementFields(const meshing::Mesh& mesh, const ThermalDofMap& map, ThermalConductivity conductivity,
                            std::span<const Temperature> temperatures) {
    if (std::optional<PostFailure> refused = checkConductivity(conductivity); refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }
    if (std::optional<PostFailure> refused = checkNumbering(mesh, map); refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }
    if (std::optional<PostFailure> refused = checkTemperatures(map, temperatures, 0); refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }
    const std::expected<ElementTable, PostFailure> table = buildElementTable(mesh, map);
    if (!table.has_value()) {
        return std::unexpected(table.error());
    }
    return fieldsFrom(*table, conductivity, temperatures, 0);
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

ThermalPostSource postSourceOf(const SteadySolutionSource& source) {
    return ThermalPostSource{.mode = ThermalAnalysisMode::Steady,
                             .system = source.system,
                             .temperatures = source.temperatures,
                             .settings = source.settings,
                             .initial = std::nullopt,
                             .transient = std::nullopt};
}

ThermalPostSource postSourceOf(const TransientSolutionSource& source) {
    return ThermalPostSource{.mode = ThermalAnalysisMode::Transient,
                             .system = source.system,
                             .temperatures = source.temperatures,
                             .settings = source.settings,
                             .initial = source.initial,
                             .transient = source.transient};
}

namespace {

/// A post source viewed as the transient solve's source, so that the comparison (and the
/// vocabulary of the reasons) is that solve's own. A steady source has no initial condition
/// or time settings, which compare as the same default on both sides.
[[nodiscard]] TransientSolutionSource asTransientSource(const ThermalPostSource& source) {
    return TransientSolutionSource{.system = source.system,
                                   .temperatures = source.temperatures,
                                   .initial = source.initial.value_or(UniformInitialTemperature{}),
                                   .transient = source.transient.value_or(TransientSettings{}),
                                   .settings = source.settings};
}

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

std::vector<TransientStaleReason> staleReasons(const ThermalPostSource& recovered, const ThermalPostSource& current) {
    std::vector<TransientStaleReason> reasons = staleReasons(asTransientSource(recovered), asTransientSource(current));
    // The mode is part of the system's source already; a post source that disagrees only in the
    // stored mode (not reachable through the factories) is still a difference.
    if (recovered.mode != current.mode && std::ranges::find(reasons, TransientStaleReason::Mode) == reasons.end()) {
        reasons.insert(reasons.begin(), TransientStaleReason::Mode);
    }
    return reasons;
}

// ---------------------------------------------------------------------------
// ThermalPostResult
// ---------------------------------------------------------------------------

std::optional<std::size_t> ThermalPostResult::ordinalOf(meshing::ElementId element) const noexcept {
    const auto found = std::lower_bound(elementIds_.begin(), elementIds_.end(), element);
    if (found == elementIds_.end() || !(*found == element)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - elementIds_.begin());
}

Result<Temperature> ThermalPostResult::temperatureOf(std::size_t state, const ThermalDofMap& map,
                                                     meshing::NodeId node) const {
    if (!describes(map)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this result belongs to another numbering, so a node cannot be looked up through it");
    }
    if (state >= states_.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("state {} is outside this result's {} states", state, states_.size()));
    }
    const Result<ThermalDofIndex> dof = map.dofOf(node);
    if (!dof.has_value()) {
        return std::unexpected(dof.error());
    }
    return states_[state].temperatures[static_cast<std::size_t>(dof->value())];
}

namespace {

[[nodiscard]] Result<std::size_t> locate(const ThermalPostResult& result, std::size_t state,
                                         meshing::ElementId element) {
    if (state >= result.stateCount()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("state {} is outside this result's {} states", state, result.stateCount()));
    }
    const std::optional<std::size_t> ordinal = result.ordinalOf(element);
    if (!ordinal.has_value()) {
        return makeError(ErrorCode::NotFound, std::format("element {} is not a tetrahedron of this result", element));
    }
    return *ordinal;
}

} // namespace

Result<TemperatureGradient3> ThermalPostResult::gradientOf(std::size_t state, meshing::ElementId element) const {
    const Result<std::size_t> ordinal = locate(*this, state, element);
    if (!ordinal.has_value()) {
        return std::unexpected(ordinal.error());
    }
    return states_[state].fields.gradients[*ordinal];
}

Result<HeatFlux3> ThermalPostResult::heatFluxOf(std::size_t state, meshing::ElementId element) const {
    const Result<std::size_t> ordinal = locate(*this, state, element);
    if (!ordinal.has_value()) {
        return std::unexpected(ordinal.error());
    }
    return states_[state].fields.heatFluxes[*ordinal];
}

Result<HeatFlux> ThermalPostResult::heatFluxMagnitudeOf(std::size_t state, meshing::ElementId element) const {
    const Result<std::size_t> ordinal = locate(*this, state, element);
    if (!ordinal.has_value()) {
        return std::unexpected(ordinal.error());
    }
    return states_[state].fields.heatFluxMagnitudes[*ordinal];
}

// ---------------------------------------------------------------------------
// The solution seam
// ---------------------------------------------------------------------------

namespace {

/// The checks the two solution seams share, in order, before any element.
[[nodiscard]] std::optional<PostFailure> checkSolutionSeam(const meshing::Mesh& mesh, const ThermalDofMap& map,
                                                           ThermalConductivity conductivity,
                                                           const ThermalSystemSource& system) {
    if (std::optional<PostFailure> refused = checkConductivity(conductivity); refused.has_value()) {
        return refused;
    }
    // THE STAMP, NOT THE COUNTS: a remesh with the same node count and the same ids has another stamp.
    if (!mesh.owns(system.mesh) || mesh.nodeCount() != system.nodeCount) {
        return failure(PostProblem::PostSourceMismatch, ErrorCode::FailedPrecondition,
                       "the solution was computed on another mesh (or another generation of this one) than the mesh "
                       "presented; it is not recovered, although the counts and ids may agree");
    }
    return checkNumbering(mesh, map);
}

} // namespace

std::expected<ThermalPostResult, PostFailure> recoverSteadyFromSolution(const meshing::Mesh& mesh,
                                                                         const ThermalDofMap& map,
                                                                         ThermalConductivity conductivity,
                                                                         const SteadyThermalSolution& solution) {
    if (std::optional<PostFailure> refused = checkSolutionSeam(mesh, map, conductivity, solution.source().system);
        refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }
    if (std::optional<PostFailure> refused = checkTemperatures(map, solution.temperatures(), 0); refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }
    const std::expected<ElementTable, PostFailure> table = buildElementTable(mesh, map);
    if (!table.has_value()) {
        return std::unexpected(table.error());
    }
    std::expected<ThermalElementFields, PostFailure> fields = fieldsFrom(*table, conductivity, solution.temperatures(), 0);
    if (!fields.has_value()) {
        return std::unexpected(std::move(fields.error()));
    }
    std::vector<ThermalPostState> states;
    states.push_back(ThermalPostState{.time = std::nullopt,
                                      .temperatures = std::vector<Temperature>(solution.temperatures().begin(),
                                                                               solution.temperatures().end()),
                                      .fields = std::move(*fields)});
    // Published only here, through the one private constructor: a refused recovery leaves nothing behind.
    return ThermalPostResult{postSourceOf(solution.source()), static_cast<std::size_t>(map.nodeCount()), table->ids,
                             std::move(states)};
}

std::expected<ThermalPostResult, PostFailure> recoverTransientFromSolution(const meshing::Mesh& mesh,
                                                                            const ThermalDofMap& map,
                                                                            ThermalConductivity conductivity,
                                                                            const TransientThermalSolution& solution) {
    if (std::optional<PostFailure> refused = checkSolutionSeam(mesh, map, conductivity, solution.source().system);
        refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }
    // Every state is checked before the first element is touched: a bad late state fails the whole history.
    for (std::size_t i = 0; i < solution.sampleCount(); ++i) {
        if (std::optional<PostFailure> refused = checkTemperatures(map, solution.temperaturesAt(i), i);
            refused.has_value()) {
            return std::unexpected(std::move(*refused));
        }
    }
    const std::expected<ElementTable, PostFailure> table = buildElementTable(mesh, map);
    if (!table.has_value()) {
        return std::unexpected(table.error());
    }
    std::vector<ThermalPostState> states;
    states.reserve(solution.sampleCount());
    // In the solution's own state order: nothing is sorted, merged or skipped.
    for (std::size_t i = 0; i < solution.sampleCount(); ++i) {
        const std::span<const Temperature> temperatures = solution.temperaturesAt(i);
        std::expected<ThermalElementFields, PostFailure> fields = fieldsFrom(*table, conductivity, temperatures, i);
        if (!fields.has_value()) {
            return std::unexpected(std::move(fields.error()));
        }
        states.push_back(ThermalPostState{.time = solution.timeOf(i),
                                          .temperatures = std::vector<Temperature>(temperatures.begin(), temperatures.end()),
                                          .fields = std::move(*fields)});
    }
    return ThermalPostResult{postSourceOf(solution.source()), static_cast<std::size_t>(map.nodeCount()), table->ids,
                             std::move(states)};
}

// ---------------------------------------------------------------------------
// The model-level entry points
// ---------------------------------------------------------------------------

namespace {

/// Check 2 and 3: the material resolves for @p mode, and the model's conductivity is the
/// document's now. Returns the conductivity to recover with, or the refusal.
[[nodiscard]] std::expected<ThermalConductivity, PostFailure>
currentConductivity(const Document& document, const ThermalModel& model, ThermalAnalysisMode mode) {
    const Result<ThermalMaterial> material = resolveThermalMaterial(document, model.body(), mode);
    if (!material.has_value()) {
        return std::unexpected(failure(PostProblem::MaterialMismatch, ErrorCode::FailedPrecondition,
                                       std::format("the material does not resolve for this analysis now: {}",
                                                   material.error().message)));
    }
    // A ThermalModel read k once, when it was built. If the document's k moved since, pairing that old k with a
    // solution that is current for the new k would label a wrong flux current.
    if (!(model.conductivity() == material->conductivity())) {
        return std::unexpected(failure(PostProblem::MaterialMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal model carries a conductivity that is not the document's now (the "
                                       "material was edited after the model was prepared); prepare the model again"));
    }
    return material->conductivity();
}

[[nodiscard]] PostFailure staleSolution(const Error& error) {
    return PostFailure{.problem = PostProblem::PostSourceMismatch, .error = error};
}

[[nodiscard]] std::expected<ThermalPostResult, PostFailure>
runSteady(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
          const SteadyThermalSolution& solution, std::span<const ThermalLoad> loads,
          std::span<const ThermalBoundaryCondition> conditions, const ThermalSolverSettings& settings) {
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, settings);
        !current.has_value()) {
        return std::unexpected(staleSolution(current.error()));
    }
    const std::expected<ThermalConductivity, PostFailure> conductivity =
        currentConductivity(document, model, ThermalAnalysisMode::Steady);
    if (!conductivity.has_value()) {
        return std::unexpected(conductivity.error());
    }
    return recoverSteadyFromSolution(model.mesh().mesh(), map, *conductivity, solution);
}

[[nodiscard]] std::expected<ThermalPostResult, PostFailure>
runTransient(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
             const TransientThermalSolution& solution, std::span<const ThermalLoad> loads,
             std::span<const ThermalBoundaryCondition> conditions, const UniformInitialTemperature& initial,
             const TransientSettings& transient, const ThermalSolverSettings& settings) {
    if (Result<void> current = requireCurrent(solution, document, model, loads, conditions, initial, transient, settings);
        !current.has_value()) {
        return std::unexpected(staleSolution(current.error()));
    }
    const std::expected<ThermalConductivity, PostFailure> conductivity =
        currentConductivity(document, model, ThermalAnalysisMode::Transient);
    if (!conductivity.has_value()) {
        return std::unexpected(conductivity.error());
    }
    return recoverTransientFromSolution(model.mesh().mesh(), map, *conductivity, solution);
}

} // namespace

Result<ThermalPostResult> recoverSteadyThermal(const Document& document, const ThermalModel& model,
                                               const ThermalDofMap& map, const SteadyThermalSolution& solution,
                                               std::span<const ThermalLoad> loads,
                                               std::span<const ThermalBoundaryCondition> conditions,
                                               const ThermalSolverSettings& settings) {
    std::expected<ThermalPostResult, PostFailure> recovered =
        runSteady(document, model, map, solution, loads, conditions, settings);
    if (!recovered.has_value()) {
        return std::unexpected(std::move(recovered.error().error));
    }
    return std::move(*recovered);
}

Result<ThermalPostResult> recoverTransientThermal(const Document& document, const ThermalModel& model,
                                                  const ThermalDofMap& map, const TransientThermalSolution& solution,
                                                  std::span<const ThermalLoad> loads,
                                                  std::span<const ThermalBoundaryCondition> conditions,
                                                  const UniformInitialTemperature& initial,
                                                  const TransientSettings& transient,
                                                  const ThermalSolverSettings& settings) {
    std::expected<ThermalPostResult, PostFailure> recovered =
        runTransient(document, model, map, solution, loads, conditions, initial, transient, settings);
    if (!recovered.has_value()) {
        return std::unexpected(std::move(recovered.error().error));
    }
    return std::move(*recovered);
}

std::optional<PostFailure> steadyRecoveryFailure(const Document& document, const ThermalModel& model,
                                                 const ThermalDofMap& map, const SteadyThermalSolution& solution,
                                                 std::span<const ThermalLoad> loads,
                                                 std::span<const ThermalBoundaryCondition> conditions,
                                                 const ThermalSolverSettings& settings) {
    std::expected<ThermalPostResult, PostFailure> recovered =
        runSteady(document, model, map, solution, loads, conditions, settings);
    if (recovered.has_value()) {
        return std::nullopt;
    }
    return std::move(recovered.error());
}

std::optional<PostFailure> transientRecoveryFailure(const Document& document, const ThermalModel& model,
                                                    const ThermalDofMap& map, const TransientThermalSolution& solution,
                                                    std::span<const ThermalLoad> loads,
                                                    std::span<const ThermalBoundaryCondition> conditions,
                                                    const UniformInitialTemperature& initial,
                                                    const TransientSettings& transient,
                                                    const ThermalSolverSettings& settings) {
    std::expected<ThermalPostResult, PostFailure> recovered =
        runTransient(document, model, map, solution, loads, conditions, initial, transient, settings);
    if (recovered.has_value()) {
        return std::nullopt;
    }
    return std::move(recovered.error());
}

// ---------------------------------------------------------------------------
// Currentness of a held post result
// ---------------------------------------------------------------------------

Result<void> requireCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                            const ThermalSolverSettings& settings) {
    if (result.mode() != ThermalAnalysisMode::Steady) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this result was recovered from a transient history; ask with the transient settings");
    }
    const Result<SteadySolutionSource> current = currentSteadySolutionSource(document, model, loads, conditions, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the recovered fields cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the recovered fields no longer describe the model: {} changed. Solve and recover "
                                 "again",
                                 reasonsText(reasons)));
}

Result<void> requireCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                            const UniformInitialTemperature& initial, const TransientSettings& transient,
                            const ThermalSolverSettings& settings) {
    if (result.mode() != ThermalAnalysisMode::Transient) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this result was recovered from a steady solution; ask without the transient settings");
    }
    const Result<TransientSolutionSource> current =
        currentTransientSolutionSource(document, model, loads, conditions, initial, transient, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the recovered fields cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<TransientStaleReason> reasons = staleReasons(result.source(), postSourceOf(*current));
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the recovered fields no longer describe the model: {} changed. Solve and recover "
                                 "again",
                                 reasonsText(reasons)));
}

bool isCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const ThermalSolverSettings& settings) {
    return requireCurrent(result, document, model, loads, conditions, settings).has_value();
}

bool isCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const UniformInitialTemperature& initial, const TransientSettings& transient,
               const ThermalSolverSettings& settings) {
    return requireCurrent(result, document, model, loads, conditions, initial, transient, settings).has_value();
}

} // namespace bettercad::thermal
