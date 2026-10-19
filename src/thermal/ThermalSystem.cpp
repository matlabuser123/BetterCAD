// Deterministic sparse assembly of the global thermal system (P18-ASSEMBLY-001).
//
// TWO PASSES, AND THE REASON IS BOTH MEMORY AND DETERMINISM (P17-ASSEMBLY-001's
// design, adopted).
//
//   SYMBOLIC   walk the elements and the convection facets once and record, per
//              row, which columns that row will ever receive. Sort and unique each
//              row, then lay out the CSR. Cost O(nnz); a triplet staging would have
//              cost sixteen records per element
//   NUMERIC    walk them again and accumulate `values[slot] += v`. There is exactly
//              ONE slot per (row, column), so a duplicate contribution is a sum by
//              construction -- not by a library's reduction policy, whose summation
//              order is not specified
//
// So the accumulation order IS the traversal order, and it is frozen in
// ThermalSystem.hpp. No unordered container is used anywhere.
//
// NO INDEX ARITHMETIC. Every row comes from `ThermalDofMap::elementDofs` (for a
// tetrahedron) or from a `ConvectionFacet`'s own unknowns (for a facet), both in the
// element's own node order. Local degrees of freedom are NEVER sorted: the local
// matrix is in the element's order, so the rows must be as well.
//
// NO FORMULATION. This file multiplies nothing. `Ke` and `Ce` are P18-ELEM-001's,
// the nodal heat input is P18-LOAD-001's, `Kconv` and `Qconv` are P18-BC-001's. It
// scatters, validates and records where everything came from.
//
// NO REGULARISATION OF ANY KIND. No penalty, no epsilon on a diagonal, no pinned
// unknown, no threshold below which an entry is dropped, no symmetrisation. A
// conduction-only K is singular and stays singular.

#include <bettercad/thermal/ThermalSystem.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::thermal {
namespace {

using Index = ThermalSparseMatrix::Index;

/// `max |K(i,j) - K(j,i)| <= this * max |K|`. The scatter writes the same local
/// numbers into both triangles in the same order, so a correct assembly is
/// symmetric to the last bit and this bound is not tight to anything: it exists so
/// that a defective scatter is a refusal and not a silent asymmetry.
constexpr double kSymmetryTolerance = 1.0e-13;

[[nodiscard]] AssemblyFailure failure(AssemblyProblem problem, ErrorCode code, std::string message) {
    return AssemblyFailure{.problem = problem, .error = makeError(code, std::move(message)).error()};
}

[[nodiscard]] std::string reasonsText(const std::vector<SystemStaleReason>& reasons) {
    std::string names;
    for (const SystemStaleReason reason : reasons) {
        if (!names.empty()) {
            names += ", ";
        }
        names += toString(reason);
    }
    return names;
}

/// A CSR layout: the row starts and the inner indices, from per-row column lists.
struct Layout {
    std::vector<Index> rowStart{};
    std::vector<Index> inner{};
};

[[nodiscard]] Layout layoutOf(std::vector<std::vector<Index>>& pattern) {
    Layout out;
    out.rowStart.assign(pattern.size() + 1, 0);
    for (std::size_t row = 0; row < pattern.size(); ++row) {
        std::vector<Index>& columns = pattern[row];
        std::ranges::sort(columns);
        columns.erase(std::ranges::unique(columns).begin(), columns.end());
        out.rowStart[row + 1] = out.rowStart[row] + static_cast<Index>(columns.size());
    }
    out.inner.reserve(static_cast<std::size_t>(out.rowStart.back()));
    for (const std::vector<Index>& columns : pattern) {
        out.inner.insert(out.inner.end(), columns.begin(), columns.end());
    }
    return out;
}

/// The slot of (row, column), or the end of the layout if the symbolic pass did not
/// reserve it (which would be a defect, and is refused rather than written past).
[[nodiscard]] std::optional<std::size_t> slotOf(const Layout& layout, std::size_t row, std::size_t column) {
    const auto first = layout.inner.begin() + static_cast<std::ptrdiff_t>(layout.rowStart[row]);
    const auto last = layout.inner.begin() + static_cast<std::ptrdiff_t>(layout.rowStart[row + 1]);
    const auto found = std::lower_bound(first, last, static_cast<Index>(column));
    if (found == last || *found != static_cast<Index>(column)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - layout.inner.begin());
}

[[nodiscard]] AssemblyFailure unreserved(std::size_t row, std::size_t column) {
    return failure(AssemblyProblem::InconsistentSystem, ErrorCode::Internal,
                   std::format("the symbolic pass did not reserve ({}, {})", row, column));
}

[[nodiscard]] AssemblyFailure fromMatrix(const ThermalSparseMatrix::Failure& refused, std::string_view which) {
    const AssemblyProblem problem = refused.problem == MatrixProblem::NonFiniteEntry
                                        ? AssemblyProblem::NonFiniteSystem
                                        : AssemblyProblem::InconsistentSystem;
    return failure(problem, ErrorCode::FailedPrecondition,
                   std::format("the assembled {} was refused: {}", which, refused.error.message));
}

[[nodiscard]] ThermalSystemSource sourceFor(const ThermalModel& model, ThermalAnalysisMode mode,
                                            const ThermalMaterial& material,
                                            std::span<const ThermalLoad> loads,
                                            std::span<const ThermalBoundaryCondition> conditions) {
    ThermalSystemSource source;
    source.body = model.body();
    source.mesh = model.mesh().mesh().stamp();
    source.nodeCount = model.mesh().mesh().nodes().size();
    source.geometry = model.geometryRevision();
    source.mode = mode;
    source.material = material.id();
    source.materialRevision = material.revision();
    source.loads.assign(loads.begin(), loads.end());
    // ONLY THE CONVECTION CONDITIONS, in their order: a prescribed temperature never
    // enters K, C or Q here (see ThermalSystemSource).
    for (const ThermalBoundaryCondition& condition : conditions) {
        if (condition.convection() != nullptr) {
            source.convections.push_back(condition);
        }
    }
    return source;
}

} // namespace

std::string_view toString(AssemblyProblem problem) noexcept {
    switch (problem) {
    case AssemblyProblem::DofMapMismatch:
        return "dof_map_mismatch";
    case AssemblyProblem::LoadSourceMismatch:
        return "load_source_mismatch";
    case AssemblyProblem::BoundarySourceMismatch:
        return "boundary_source_mismatch";
    case AssemblyProblem::MaterialUnavailable:
        return "material_unavailable";
    case AssemblyProblem::MaterialSourceMismatch:
        return "material_source_mismatch";
    case AssemblyProblem::MeshHasNoElements:
        return "mesh_has_no_elements";
    case AssemblyProblem::LoadVectorMismatch:
        return "load_vector_mismatch";
    case AssemblyProblem::BoundaryDofOutOfRange:
        return "boundary_dof_out_of_range";
    case AssemblyProblem::ElementRejected:
        return "element_rejected";
    case AssemblyProblem::NonFiniteSystem:
        return "non_finite_system";
    case AssemblyProblem::InconsistentSystem:
        return "inconsistent_system";
    }
    return "unknown";
}

std::string_view toString(SystemStaleReason reason) noexcept {
    switch (reason) {
    case SystemStaleReason::Body:
        return "body";
    case SystemStaleReason::Mesh:
        return "mesh";
    case SystemStaleReason::Geometry:
        return "geometry";
    case SystemStaleReason::Mode:
        return "mode";
    case SystemStaleReason::Material:
        return "material";
    case SystemStaleReason::Loads:
        return "loads";
    case SystemStaleReason::Convections:
        return "convections";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

std::vector<SystemStaleReason> staleReasons(const ThermalSystemSource& assembled, const ThermalSystemSource& current) {
    std::vector<SystemStaleReason> reasons;
    if (assembled.body != current.body) {
        reasons.push_back(SystemStaleReason::Body);
    }
    if (!(assembled.mesh == current.mesh) || assembled.nodeCount != current.nodeCount) {
        reasons.push_back(SystemStaleReason::Mesh);
    }
    if (!(assembled.geometry == current.geometry)) {
        reasons.push_back(SystemStaleReason::Geometry);
    }
    if (assembled.mode != current.mode) {
        reasons.push_back(SystemStaleReason::Mode);
    }
    if (assembled.material != current.material || assembled.materialRevision != current.materialRevision) {
        reasons.push_back(SystemStaleReason::Material);
    }
    if (!(assembled.loads == current.loads)) {
        reasons.push_back(SystemStaleReason::Loads);
    }
    if (!(assembled.convections == current.convections)) {
        reasons.push_back(SystemStaleReason::Convections);
    }
    return reasons;
}

Result<ThermalSystemSource> currentThermalSystemSource(const Document& document, const ThermalModel& model,
                                                       ThermalAnalysisMode mode, std::span<const ThermalLoad> loads,
                                                       std::span<const ThermalBoundaryCondition> conditions) {
    Result<ThermalMaterial> material = resolveThermalMaterial(document, model.body(), mode);
    if (!material.has_value()) {
        return std::unexpected(material.error());
    }
    return sourceFor(model, mode, *material, loads, conditions);
}

bool isCurrent(const ThermalSystem& system, const Document& document, const ThermalModel& model,
               ThermalAnalysisMode mode, std::span<const ThermalLoad> loads,
               std::span<const ThermalBoundaryCondition> conditions) {
    return requireCurrent(system, document, model, mode, loads, conditions).has_value();
}

Result<void> requireCurrent(const ThermalSystem& system, const Document& document, const ThermalModel& model,
                            ThermalAnalysisMode mode, std::span<const ThermalLoad> loads,
                            std::span<const ThermalBoundaryCondition> conditions) {
    const Result<ThermalSystemSource> current = currentThermalSystemSource(document, model, mode, loads, conditions);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the assembled system cannot be compared with the model: {}",
                                     current.error().message));
    }
    const std::vector<SystemStaleReason> reasons = staleReasons(system.source(), *current);
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the assembled system no longer describes the model: {} changed. Assemble "
                                 "it again",
                                 reasonsText(reasons)));
}

// ---------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------

std::expected<ThermalSystem, AssemblyFailure> assembleThermalSystemFrom(const ThermalAssemblyData& data,
                                                                       ThermalSystemSource source) {
    const meshing::Mesh& mesh = data.mesh;
    const ThermalDofMap& map = data.map;
    const std::size_t rows = static_cast<std::size_t>(map.dofCount());
    const bool transient = data.capacity.has_value();

    // THE NUMBERING FIRST: every row below is one of its unknowns.
    if (!map.describes(mesh)) {
        return std::unexpected(failure(AssemblyProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal numbering was built for a different mesh, so its unknowns "
                                       "are not this mesh's nodes"));
    }
    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
    if (tets.empty()) {
        return std::unexpected(failure(AssemblyProblem::MeshHasNoElements, ErrorCode::FailedPrecondition,
                                       "the mesh carries no tetrahedron, so there is no conduction to assemble"));
    }
    if (data.heatInput.size() != rows) {
        return std::unexpected(failure(AssemblyProblem::LoadVectorMismatch, ErrorCode::InvalidArgument,
                                       std::format("the heat-input vector has {} entries for {} unknowns; it "
                                                   "is indexed by unknown and must be exactly that long",
                                                   data.heatInput.size(), rows)));
    }
    std::size_t facetCount = 0;
    for (const PreparedConvectionBoundary& convection : data.convections) {
        for (const ConvectionFacet& facet : convection.facets) {
            for (const ThermalDofIndex dof : facet.dofs) {
                if (dof.value() >= map.dofCount()) {
                    return std::unexpected(
                        failure(AssemblyProblem::BoundaryDofOutOfRange, ErrorCode::InvalidArgument,
                                std::format("{}: convection facet {} names unknown {} of {}", convection.condition,
                                            facet.facet, dof.value(), rows)));
                }
            }
            ++facetCount;
        }
    }

    // -----------------------------------------------------------------------
    // SYMBOLIC PASS: which columns each row will ever receive.
    // -----------------------------------------------------------------------
    std::vector<std::vector<Index>> volumePattern(rows);
    for (const meshing::Tetrahedron& tet : tets) {
        const Result<std::array<ThermalDofIndex, 4>> dofs = map.elementDofs(tet);
        if (!dofs.has_value()) {
            return std::unexpected(failure(AssemblyProblem::ElementRejected, dofs.error().code,
                                           std::format("element {}: {}", tet.id, dofs.error().message)));
        }
        for (const ThermalDofIndex row : *dofs) {
            for (const ThermalDofIndex column : *dofs) {
                volumePattern[static_cast<std::size_t>(row.value())].push_back(static_cast<Index>(column.value()));
            }
        }
    }
    // C holds exactly the volume pattern. K holds it and, in addition, whatever the
    // convection facets reserve: for a valid mesh a facet's pairs are a subset of
    // its tetrahedron's, but that is P16's guarantee and not assumed here.
    std::vector<std::vector<Index>> conductancePattern = volumePattern;
    for (const PreparedConvectionBoundary& convection : data.convections) {
        for (const ConvectionFacet& facet : convection.facets) {
            for (const ThermalDofIndex row : facet.dofs) {
                for (const ThermalDofIndex column : facet.dofs) {
                    conductancePattern[static_cast<std::size_t>(row.value())].push_back(
                        static_cast<Index>(column.value()));
                }
            }
        }
    }
    const Layout conductanceLayout = layoutOf(conductancePattern);
    std::vector<double> conductanceValues(conductanceLayout.inner.size(), 0.0);

    Layout capacityLayout;
    std::vector<double> capacityValues;
    if (transient) {
        capacityLayout = layoutOf(volumePattern);
        capacityValues.assign(capacityLayout.inner.size(), 0.0);
    }

    // -----------------------------------------------------------------------
    // NUMERIC PASS: accumulate. ONE slot per (row, column), so a duplicate
    // contribution is a sum by construction.
    // -----------------------------------------------------------------------
    std::vector<meshing::ElementId> elementOrder;
    elementOrder.reserve(tets.size());
    for (const meshing::Tetrahedron& tet : tets) {
        // VALID BY CONSTRUCTION: the symbolic pass called this same pure function on
        // every element and returned on the first refusal, so none can fail here.
        const Result<std::array<ThermalDofIndex, 4>> dofs = map.elementDofs(tet);
        const Result<Tet4Corners> corners = cornersOf(mesh, tet);
        if (!corners.has_value()) {
            return std::unexpected(failure(AssemblyProblem::ElementRejected, corners.error().code,
                                           std::format("element {}: {}", tet.id, corners.error().message)));
        }
        // THE KERNELS ARE P18-ELEM'S, AND A REFUSAL IS PROPAGATED. An assembly that
        // skipped a degenerate or inverted element and carried on would publish a
        // body with a hole in it.
        const Result<Tet4Geometry> geometry = computeTet4Geometry(*corners);
        if (!geometry.has_value()) {
            return std::unexpected(failure(AssemblyProblem::ElementRejected, geometry.error().code,
                                           std::format("element {}: {}", tet.id, geometry.error().message)));
        }
        const Result<Tet4Conduction> ke = computeTet4Conduction(*geometry, data.conductivity);
        if (!ke.has_value()) {
            return std::unexpected(failure(AssemblyProblem::ElementRejected, ke.error().code,
                                           std::format("element {}: {}", tet.id, ke.error().message)));
        }
        // LOCAL ROW 0..3 THEN LOCAL COLUMN 0..3, in the element's own order. All
        // sixteen entries, with no pruning of exact zeros, so the pattern is a
        // function of connectivity alone.
        for (std::size_t a = 0; a < 4; ++a) {
            const auto row = static_cast<std::size_t>((*dofs)[a].value());
            for (std::size_t b = 0; b < 4; ++b) {
                const auto column = static_cast<std::size_t>((*dofs)[b].value());
                const std::optional<std::size_t> slot = slotOf(conductanceLayout, row, column);
                if (!slot.has_value()) {
                    return std::unexpected(unreserved(row, column));
                }
                conductanceValues[*slot] += (*ke)(a, b).si();
            }
        }
        if (transient) {
            const Result<Tet4Capacity> ce = computeTet4Capacity(*geometry, *data.capacity);
            if (!ce.has_value()) {
                return std::unexpected(failure(AssemblyProblem::ElementRejected, ce.error().code,
                                               std::format("element {}: {}", tet.id, ce.error().message)));
            }
            for (std::size_t a = 0; a < 4; ++a) {
                const auto row = static_cast<std::size_t>((*dofs)[a].value());
                for (std::size_t b = 0; b < 4; ++b) {
                    const auto column = static_cast<std::size_t>((*dofs)[b].value());
                    const std::optional<std::size_t> slot = slotOf(capacityLayout, row, column);
                    if (!slot.has_value()) {
                        return std::unexpected(unreserved(row, column));
                    }
                    capacityValues[*slot] += (*ce)(a, b).si();
                }
            }
        }
        elementOrder.push_back(tet.id);
    }

    // Q: the prepared heat input first, in unknown order; then the convection
    // ambient sources below, in condition then facet order. `+=`, so two sources on
    // one unknown add. The per-load contributions in PreparedThermalLoads are
    // ATTRIBUTION of this same vector, and are not added again.
    std::vector<double> heat(rows, 0.0);
    for (std::size_t row = 0; row < rows; ++row) {
        heat[row] += data.heatInput[row].si();
    }

    // THE ROBIN TERMS. Matrix to K, source to Q, from the same facet.
    for (const PreparedConvectionBoundary& convection : data.convections) {
        for (const ConvectionFacet& facet : convection.facets) {
            for (std::size_t a = 0; a < kTri3Nodes; ++a) {
                const auto row = static_cast<std::size_t>(facet.dofs[a].value());
                for (std::size_t b = 0; b < kTri3Nodes; ++b) {
                    const auto column = static_cast<std::size_t>(facet.dofs[b].value());
                    const std::optional<std::size_t> slot = slotOf(conductanceLayout, row, column);
                    if (!slot.has_value()) {
                        return std::unexpected(unreserved(row, column));
                    }
                    conductanceValues[*slot] += facet.term(a, b).si();
                }
                heat[row] += facet.term.source(a).si();
            }
        }
    }

    // -----------------------------------------------------------------------
    // Validate before publishing. The symmetry gate is a refusal and never a repair.
    // -----------------------------------------------------------------------
    std::expected<ThermalSparseMatrix, ThermalSparseMatrix::Failure> k = ThermalSparseMatrix::fromCsr(
        rows, rows, conductanceLayout.rowStart, conductanceLayout.inner, std::move(conductanceValues),
        kSymmetryTolerance);
    if (!k.has_value()) {
        return std::unexpected(fromMatrix(k.error(), "conductance matrix"));
    }
    std::optional<CapacityMatrix> capacity;
    if (transient) {
        std::expected<ThermalSparseMatrix, ThermalSparseMatrix::Failure> c = ThermalSparseMatrix::fromCsr(
            rows, rows, capacityLayout.rowStart, capacityLayout.inner, std::move(capacityValues),
            kSymmetryTolerance);
        if (!c.has_value()) {
            return std::unexpected(fromMatrix(c.error(), "capacity matrix"));
        }
        capacity.emplace(std::move(*c));
    }
    Result<ThermalHeatVector> q = ThermalHeatVector::fromEntries(std::move(heat));
    if (!q.has_value()) {
        return std::unexpected(failure(AssemblyProblem::NonFiniteSystem, ErrorCode::FailedPrecondition,
                                       std::format("the assembled heat input was refused: {}", q.error().message)));
    }

    // Facts of the data, not of the caller: what mesh, how many nodes, which mode.
    source.mesh = map.stamp();
    source.nodeCount = mesh.nodes().size();
    source.mode = transient ? ThermalAnalysisMode::Transient : ThermalAnalysisMode::Steady;

    // PUBLISHED IN ONE STEP, through the one private constructor.
    return ThermalSystem{ConductanceMatrix{std::move(*k)}, std::move(capacity), std::move(*q), std::move(source),
                         std::move(elementOrder), facetCount};
}

// ---------------------------------------------------------------------------
// The model-level entry
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::expected<ThermalSystem, AssemblyFailure>
run(const Document& document, const ThermalModel& model, ThermalAnalysisMode mode, const ThermalDofMap& map,
    const PreparedThermalLoads& preparedLoads, std::span<const ThermalLoad> loads,
    const PreparedThermalBoundary& preparedBoundary, std::span<const ThermalBoundaryCondition> conditions) {
    const meshing::Mesh& mesh = model.mesh().mesh();

    // ALL OF THESE BEFORE THE FIRST ELEMENT, so a stale input is reported as one and
    // never as whatever numerical symptom it would have caused.
    if (!map.describes(mesh)) {
        return std::unexpected(failure(AssemblyProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal numbering was built for a different mesh than the model's "
                                       "current one, so its unknowns are not these nodes"));
    }
    if (const std::vector<LoadStaleReason> stale =
            staleReasons(preparedLoads.source(), currentLoadSource(model, loads));
        !stale.empty()) {
        std::string names;
        for (const LoadStaleReason reason : stale) {
            names += names.empty() ? "" : ", ";
            names += toString(reason);
        }
        return std::unexpected(failure(AssemblyProblem::LoadSourceMismatch, ErrorCode::FailedPrecondition,
                                       std::format("the prepared heat input no longer describes the model or "
                                                   "the loads: {} changed",
                                                   names)));
    }
    if (const std::vector<BoundaryStaleReason> stale =
            staleReasons(preparedBoundary.source(), currentBoundarySource(model, conditions));
        !stale.empty()) {
        std::string names;
        for (const BoundaryStaleReason reason : stale) {
            names += names.empty() ? "" : ", ";
            names += toString(reason);
        }
        return std::unexpected(failure(AssemblyProblem::BoundarySourceMismatch, ErrorCode::FailedPrecondition,
                                       std::format("the prepared boundary no longer describes the model or the "
                                                   "conditions: {} changed",
                                                   names)));
    }

    // THE MATERIAL IS RESOLVED AFRESH, never passed in: it is derived, and a cached
    // one is a stale one. A steady mode needs k only; a transient needs rho and cp
    // too, and P18-MAT-001 refuses it without them, so there is no zero C.
    Result<ThermalMaterial> material = resolveThermalMaterial(document, model.body(), mode);
    if (!material.has_value()) {
        return std::unexpected(failure(AssemblyProblem::MaterialUnavailable, material.error().code,
                                       material.error().message));
    }
    if (!(material->conductivity() == model.conductivity())) {
        return std::unexpected(failure(AssemblyProblem::MaterialSourceMismatch, ErrorCode::FailedPrecondition,
                                       std::format("the model was validated against k = {} W/(m K) but the "
                                                   "document's material now has k = {}",
                                                   model.conductivity().si(), material->conductivity().si())));
    }

    const ThermalAssemblyData data{.mesh = mesh,
                                   .map = map,
                                   .conductivity = material->conductivity(),
                                   .capacity = material->capacity(),
                                   .heatInput = preparedLoads.heatInput(),
                                   .convections = preparedBoundary.convections()};
    return assembleThermalSystemFrom(data, sourceFor(model, mode, *material, loads, conditions));
}

} // namespace

Result<ThermalSystem> assembleThermalSystem(const Document& document, const ThermalModel& model,
                                            ThermalAnalysisMode mode, const ThermalDofMap& map,
                                            const PreparedThermalLoads& preparedLoads,
                                            std::span<const ThermalLoad> loads,
                                            const PreparedThermalBoundary& preparedBoundary,
                                            std::span<const ThermalBoundaryCondition> conditions) {
    std::expected<ThermalSystem, AssemblyFailure> built =
        run(document, model, mode, map, preparedLoads, loads, preparedBoundary, conditions);
    if (!built.has_value()) {
        return std::unexpected(std::move(built.error().error));
    }
    return std::move(*built);
}

Result<ThermalSystem> assembleThermalSystem(const Document& document, const ThermalModel& model,
                                            const ThermalDofMap& map, const PreparedThermalLoads& preparedLoads,
                                            const PreparedThermalBoundary& preparedBoundary,
                                            const ThermalAnalysisDefinition& definition) {
    return assembleThermalSystem(document, model, definition.mode, map, preparedLoads,
                                 std::span<const ThermalLoad>{definition.loads}, preparedBoundary,
                                 std::span<const ThermalBoundaryCondition>{definition.boundaryConditions});
}

std::optional<AssemblyFailure> thermalAssemblyFailure(const Document& document, const ThermalModel& model,
                                                      ThermalAnalysisMode mode, const ThermalDofMap& map,
                                                      const PreparedThermalLoads& preparedLoads,
                                                      std::span<const ThermalLoad> loads,
                                                      const PreparedThermalBoundary& preparedBoundary,
                                                      std::span<const ThermalBoundaryCondition> conditions) {
    std::expected<ThermalSystem, AssemblyFailure> built =
        run(document, model, mode, map, preparedLoads, loads, preparedBoundary, conditions);
    if (built.has_value()) {
        return std::nullopt;
    }
    return std::move(built.error());
}

} // namespace bettercad::thermal
