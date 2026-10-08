// Deterministic sparse assembly of the global system (P17-ASSEMBLY-001).
//
// TWO PASSES, AND THE REASON IS BOTH MEMORY AND DETERMINISM.
//
//   SYMBOLIC   walk the elements once and record, per row, which columns that
//              row will ever receive. Sort and unique each row, then lay out
//              the CSR. Cost O(nnz); a triplet staging would have cost
//              144 * Nelements * 24 bytes, which is 3.5 KB per element
//   NUMERIC    walk the elements again and accumulate `values[slot] += v`.
//              There is exactly ONE slot per (row, column), so a duplicate
//              contribution is a sum by construction -- not by a library's
//              reduction policy, whose summation order Eigen documents as
//              summed but not as summed in a specified order
//
// So the accumulation order IS the traversal order, and both orders are frozen
// and neither is this module's invention: P16 guarantees `tetrahedra()` is
// ascending by `ElementId` and built by nothing unordered, and the local order
// is written out as row 0..11 then column 0..11.
//
// NO INDEX ARITHMETIC. Every row comes from `FreeEquationMap::equationOf`,
// which ADR-037 made a zero-based POSITION for exactly this reason. There is
// no `- 1` and no `3 * node` anywhere in this file.
//
// NO ALGEBRA AND NO LIBRARY. This file multiplies nothing, factorises nothing
// and includes no linear algebra header; `bettercad_structural` links none.
// ADR-038 records why, and what P17-SOLVE-001 can do with the arrays.

#include <bettercad/structural/StructuralSystem.hpp>

#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralMaterial.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::structural {

std::string_view toString(AssemblyProblem problem) noexcept {
    switch (problem) {
    case AssemblyProblem::MeshHasNoDegreesOfFreedom:
        return "mesh_has_no_degrees_of_freedom";
    case AssemblyProblem::MeshHasNoElements:
        return "mesh_has_no_elements";
    case AssemblyProblem::ElementNodeMissing:
        return "element_node_missing";
    case AssemblyProblem::ElementRejected:
        return "element_rejected";
    case AssemblyProblem::LoadSourceMismatch:
        return "load_source_mismatch";
    case AssemblyProblem::LoadNodeMissing:
        return "load_node_missing";
    case AssemblyProblem::NonFiniteSystem:
        return "non_finite_system";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// StiffnessMatrix
// ---------------------------------------------------------------------------

Stiffness StiffnessMatrix::coeff(std::size_t row, std::size_t column) const noexcept {
    if (row >= rows_ || column >= columns_) {
        return Stiffness{};
    }
    const Index begin = rowStart_[row];
    const Index end = rowStart_[row + 1];
    // The inner indices of a row are strictly ascending, which is what makes
    // this a search rather than a scan.
    const auto first = inner_.begin() + static_cast<std::ptrdiff_t>(begin);
    const auto last = inner_.begin() + static_cast<std::ptrdiff_t>(end);
    const auto found = std::lower_bound(first, last, static_cast<Index>(column));
    if (found == last || *found != static_cast<Index>(column)) {
        return Stiffness{};
    }
    return Stiffness::fromSi(values_[static_cast<std::size_t>(found - inner_.begin())]);
}

double StiffnessMatrix::largestSymmetryError() const noexcept {
    double largest = 0.0;
    // OVER STORED ENTRIES, never over a transpose and never over a dense
    // copy: a large mesh must not be densified to be checked. The pattern is
    // symmetric by construction, so every stored (i,j) has a stored (j,i) and
    // coeff() finds it in O(log k).
    for (std::size_t row = 0; row < rows_; ++row) {
        for (Index slot = rowStart_[row]; slot < rowStart_[row + 1]; ++slot) {
            const std::size_t column = static_cast<std::size_t>(inner_[slot]);
            const double mirrored = coeff(column, row).si();
            largest = std::max(largest, std::abs(values_[slot] - mirrored));
        }
    }
    return largest;
}

double StiffnessMatrix::largestMagnitude() const noexcept {
    double largest = 0.0;
    for (const double value : values_) {
        largest = std::max(largest, std::abs(value));
    }
    return largest;
}

// ---------------------------------------------------------------------------
// ForceVector
// ---------------------------------------------------------------------------

Force3D ForceVector::resultantForce() const noexcept {
    // GROUPED BY THE FROZEN INTERLEAVING: row 3k+c carries component c of node
    // k, so the component of a row is its position modulo three -- which is
    // `componentAt`, P17-DOF's own cyclic lookup, rather than a second opinion
    // about the layout.
    std::array<double, kDofsPerNode> total{};
    for (std::size_t row = 0; row < entries_.size(); ++row) {
        total[row % kDofsPerNode] += entries_[row];
    }
    static_assert(offsetOf(DofComponent::Ux) == 0);
    static_assert(offsetOf(DofComponent::Uy) == 1);
    static_assert(offsetOf(DofComponent::Uz) == 2);
    return Force3D{Force::fromSi(total[0]), Force::fromSi(total[1]), Force::fromSi(total[2])};
}

// ---------------------------------------------------------------------------
// Local to global
// ---------------------------------------------------------------------------

Result<std::array<DofIndex, kTet4Dofs>>
elementDegreesOfFreedom(const MeshDofMap& numbering,
                        const std::array<meshing::NodeId, kTet4Nodes>& nodes) {
    std::array<DofIndex, kTet4Dofs> dofs{};
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        // THE THREE COMPONENTS OF ONE NODE, FROM P17-DOF. `indicesOf` returns
        // them in `kDofComponents` order, which is the same order
        // `localDofIndex` uses -- the static_asserts below pin that the two
        // agree rather than leaving it to a reader.
        Result<std::array<DofIndex, kDofsPerNode>> nodal = numbering.indicesOf(nodes[node]);
        if (!nodal.has_value()) {
            return std::unexpected(nodal.error());
        }
        for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
            dofs[localDofIndex(node, kDofComponents[offset])] = (*nodal)[offset];
        }
    }
    return dofs;
}

static_assert(localDofIndex(0, DofComponent::Ux) == 0);
static_assert(localDofIndex(0, DofComponent::Uz) == 2);
static_assert(localDofIndex(1, DofComponent::Ux) == 3);
static_assert(localDofIndex(2, DofComponent::Uy) == 7);
static_assert(localDofIndex(3, DofComponent::Uz) == 11);

namespace {

/// The working state of one assembly, and the only place a partially built
/// system exists. Nothing escapes it until every check has passed.
struct Working {
    std::optional<AssemblyProblem> problem{};
    Error error{};

    std::size_t rows = 0;
    std::vector<StiffnessMatrix::Index> rowStart{};
    std::vector<StiffnessMatrix::Index> inner{};
    std::vector<double> values{};
    std::vector<double> force{};
    std::vector<meshing::ElementId> elements{};
};

[[nodiscard]] Working fail(AssemblyProblem problem, Error error) {
    return Working{.problem = problem, .error = std::move(error)};
}

/// The twelve global rows of one tetrahedron, or the reason there are none.
struct ElementRows {
    std::array<std::size_t, kTet4Dofs> rows{};
    std::array<Point3D, kTet4Nodes> corners{};
};

[[nodiscard]] Result<ElementRows> rowsOf(const meshing::Mesh& mesh, const MeshDofMap& numbering,
                                         const FreeEquationMap& equations,
                                         const meshing::Tetrahedron& tet) {
    ElementRows out;
    for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
        const meshing::Node* node = mesh.findNode(tet.nodes[corner]);
        if (node == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("element {} names {}, which is not a node of this mesh",
                                         tet.id, tet.nodes[corner]));
        }
        out.corners[corner] = node->position;
    }

    Result<std::array<DofIndex, kTet4Dofs>> dofs = elementDegreesOfFreedom(numbering, tet.nodes);
    if (!dofs.has_value()) {
        return std::unexpected(dofs.error());
    }
    for (std::size_t local = 0; local < kTet4Dofs; ++local) {
        // THE ROW IS A POSITION AND COMES FROM P17-DOF, never from
        // arithmetic. With nothing constrained every degree of freedom is
        // free, so this never fails -- and it is propagated rather than
        // assumed, because a missing equation would otherwise scatter into
        // row zero.
        const std::optional<FreeEquationIndex> equation = equations.equationOf((*dofs)[local]);
        if (!equation.has_value()) {
            return makeError(ErrorCode::Internal,
                             std::format("element {}: its local degree of freedom {} has no "
                                         "equation in the unconstrained numbering",
                                         tet.id, local));
        }
        out.rows[local] = static_cast<std::size_t>(equation->value());
    }
    return out;
}

/// One assembly, shared by `assembleStructuralSystem` and
/// `structuralAssemblyProblem` so the two cannot drift apart.
[[nodiscard]] Working run(const StructuralModel& model, const StructuralMaterial& material,
                          const PreparedLoads& loads, const MeshDofMap& numbering,
                          const FreeEquationMap& equations) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();

    if (tets.empty()) {
        return fail(AssemblyProblem::MeshHasNoElements,
                    makeError(ErrorCode::FailedPrecondition,
                              "the mesh carries no tetrahedron, so there is no stiffness to "
                              "assemble")
                        .error());
    }

    // THE LOADS MUST BE THIS MESH'S. Stamp AND node count, for the reason
    // PreparedLoads::describes records -- and this is the check that stops an
    // M1 load vector being assembled with M2 stiffness because some NodeIds
    // happen to overlap.
    if (!loads.describes(mesh)) {
        return fail(AssemblyProblem::LoadSourceMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              "the prepared loads were built for a different mesh from the one "
                              "this analysis holds, so their nodal forces would land on "
                              "unrelated material")
                        .error());
    }

    Result<ElasticityMatrix> elasticity = isotropicElasticity(material.elastic());
    if (!elasticity.has_value()) {
        return fail(AssemblyProblem::ElementRejected, elasticity.error());
    }

    Working out;
    out.rows = equations.freeCount();
    out.elements.reserve(tets.size());

    // -----------------------------------------------------------------------
    // SYMBOLIC PASS: which columns each row will ever receive.
    // -----------------------------------------------------------------------
    std::vector<std::vector<StiffnessMatrix::Index>> pattern(out.rows);
    for (const meshing::Tetrahedron& tet : tets) {
        Result<ElementRows> rows = rowsOf(mesh, numbering, equations, tet);
        if (!rows.has_value()) {
            return fail(AssemblyProblem::ElementNodeMissing, rows.error());
        }
        for (const std::size_t row : rows->rows) {
            for (const std::size_t column : rows->rows) {
                pattern[row].push_back(static_cast<StiffnessMatrix::Index>(column));
            }
        }
    }

    out.rowStart.resize(out.rows + 1, 0);
    for (std::size_t row = 0; row < out.rows; ++row) {
        std::vector<StiffnessMatrix::Index>& columns = pattern[row];
        std::ranges::sort(columns);
        columns.erase(std::ranges::unique(columns).begin(), columns.end());
        out.rowStart[row + 1] =
            out.rowStart[row] + static_cast<StiffnessMatrix::Index>(columns.size());
    }

    const std::size_t nonZeros = static_cast<std::size_t>(out.rowStart.back());
    out.inner.reserve(nonZeros);
    for (const std::vector<StiffnessMatrix::Index>& columns : pattern) {
        out.inner.insert(out.inner.end(), columns.begin(), columns.end());
    }
    // Explicitly zero: every slot is written by `+=`, so an unwritten one must
    // read as zero and not as whatever the allocator returned.
    out.values.assign(nonZeros, 0.0);

    // -----------------------------------------------------------------------
    // NUMERIC PASS: accumulate. ONE slot per (row, column), so a duplicate
    // contribution is a sum by construction.
    // -----------------------------------------------------------------------
    for (const meshing::Tetrahedron& tet : tets) {
        Result<ElementRows> rows = rowsOf(mesh, numbering, equations, tet);
        if (!rows.has_value()) {
            return fail(AssemblyProblem::ElementNodeMissing, rows.error());
        }

        // Ke IS P17-ELEM'S, AND A REFUSAL IS PROPAGATED. An assembly that
        // skipped a degenerate or inverted element and carried on would
        // publish a softer body with nothing reporting it.
        Result<Tet4Kinematics> kinematics = computeTet4Kinematics(rows->corners);
        if (!kinematics.has_value()) {
            return fail(AssemblyProblem::ElementRejected,
                        makeError(kinematics.error().code,
                                  std::format("element {}: {}", tet.id,
                                              kinematics.error().message))
                            .error());
        }
        Result<Tet4Stiffness> ke = computeTet4Stiffness(*kinematics, *elasticity);
        if (!ke.has_value()) {
            return fail(AssemblyProblem::ElementRejected,
                        makeError(ke.error().code,
                                  std::format("element {}: {}", tet.id, ke.error().message))
                            .error());
        }

        // LOCAL ROW 0..11 THEN LOCAL COLUMN 0..11, written out. All 144
        // entries, with no pruning of exact zeros, so the pattern is a
        // function of connectivity alone.
        for (std::size_t a = 0; a < kTet4Dofs; ++a) {
            const std::size_t row = rows->rows[a];
            const StiffnessMatrix::Index begin = out.rowStart[row];
            const StiffnessMatrix::Index end = out.rowStart[row + 1];
            for (std::size_t b = 0; b < kTet4Dofs; ++b) {
                const auto column = static_cast<StiffnessMatrix::Index>(rows->rows[b]);
                const auto first = out.inner.begin() + static_cast<std::ptrdiff_t>(begin);
                const auto last = out.inner.begin() + static_cast<std::ptrdiff_t>(end);
                const auto slot = std::lower_bound(first, last, column);
                // The symbolic pass put this column in this row, so the
                // search cannot miss. Checked rather than assumed: a missed
                // slot would otherwise write past the row.
                if (slot == last || *slot != column) {
                    return fail(AssemblyProblem::NonFiniteSystem,
                                makeError(ErrorCode::Internal,
                                          std::format("element {}: the symbolic pass did not "
                                                      "reserve ({}, {})",
                                                      tet.id, row, column))
                                    .error());
                }
                out.values[static_cast<std::size_t>(slot - out.inner.begin())] +=
                    ke->operator()(a, b).si();
            }
        }
        out.elements.push_back(tet.id);
    }

    // -----------------------------------------------------------------------
    // F: the prepared nodal forces, in their own ascending order.
    // -----------------------------------------------------------------------
    out.force.assign(out.rows, 0.0);
    for (const NodalLoad& load : loads.nodal()) {
        Result<std::array<DofIndex, kDofsPerNode>> dofs = numbering.indicesOf(load.node);
        if (!dofs.has_value()) {
            return fail(AssemblyProblem::LoadNodeMissing,
                        makeError(ErrorCode::NotFound,
                                  std::format("a prepared load names {}, which the current "
                                              "numbering does not have",
                                              load.node))
                            .error());
        }
        const std::array<double, kDofsPerNode> components{load.force.x.si(), load.force.y.si(),
                                                          load.force.z.si()};
        for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
            const std::optional<FreeEquationIndex> equation = equations.equationOf((*dofs)[offset]);
            if (!equation.has_value()) {
                return fail(AssemblyProblem::LoadNodeMissing,
                            makeError(ErrorCode::Internal,
                                      std::format("a prepared load on {} has no equation in the "
                                                  "unconstrained numbering",
                                                  load.node))
                                .error());
            }
            // `+=`, so two loads on one degree of freedom add.
            out.force[static_cast<std::size_t>(equation->value())] += components[offset];
        }
    }

    // -----------------------------------------------------------------------
    // Validate before publishing.
    // -----------------------------------------------------------------------
    for (const double value : out.values) {
        if (!std::isfinite(value)) {
            return fail(AssemblyProblem::NonFiniteSystem,
                        makeError(ErrorCode::FailedPrecondition,
                                  "an assembled stiffness entry is not finite")
                            .error());
        }
    }
    for (const double value : out.force) {
        if (!std::isfinite(value)) {
            return fail(AssemblyProblem::NonFiniteSystem,
                        makeError(ErrorCode::FailedPrecondition, "an assembled force entry is not finite")
                            .error());
        }
    }
    return out;
}

/// The numbering and the identity free-equation map of @p mesh.
///
/// THE ROW SPACE IS THE FREE NUMBERING OF THE EMPTY CONSTRAINT SET, which is
/// what gives the assembly a zero-based position for every degree of freedom
/// without a single `- 1`. With nothing prescribed, `freeCount() ==
/// dofCount()` and the free numbering IS the global one.
struct Numbering {
    std::optional<AssemblyProblem> problem{};
    Error error{};
    std::optional<MeshDofMap> map{};
    std::optional<FreeEquationMap> equations{};
};

[[nodiscard]] Numbering numberingOf(const meshing::Mesh& mesh) {
    Numbering out;
    Result<MeshDofMap> map = structural::buildMeshDofMap(mesh);
    if (!map.has_value()) {
        out.problem = AssemblyProblem::MeshHasNoDegreesOfFreedom;
        out.error = map.error();
        return out;
    }
    Result<ConstraintSet> unconstrained = buildConstraintSet(*map, {});
    if (!unconstrained.has_value()) {
        out.problem = AssemblyProblem::MeshHasNoDegreesOfFreedom;
        out.error = unconstrained.error();
        return out;
    }
    Result<FreeEquationMap> equations = buildFreeEquationMap(*map, *unconstrained);
    if (!equations.has_value()) {
        out.problem = AssemblyProblem::MeshHasNoDegreesOfFreedom;
        out.error = equations.error();
        return out;
    }
    out.map = std::move(*map);
    out.equations = std::move(*equations);
    return out;
}

} // namespace

Result<GlobalStructuralSystem> assembleStructuralSystem(const StructuralModel& model,
                                                        const StructuralMaterial& material,
                                                        const PreparedLoads& loads) {
    Numbering numbering = numberingOf(model.mesh().mesh());
    if (numbering.problem.has_value()) {
        return std::unexpected(std::move(numbering.error));
    }

    Working working = run(model, material, loads, *numbering.map, *numbering.equations);
    if (working.problem.has_value()) {
        return std::unexpected(std::move(working.error));
    }

    // PUBLISHED IN ONE STEP, through the one private constructor. Nothing
    // above this line has touched a GlobalStructuralSystem, so a refused
    // assembly cannot leave a half-filled K behind.
    StiffnessMatrix stiffness;
    stiffness.rows_ = working.rows;
    stiffness.columns_ = working.rows;
    stiffness.rowStart_ = std::move(working.rowStart);
    stiffness.inner_ = std::move(working.inner);
    stiffness.values_ = std::move(working.values);

    ForceVector force;
    force.entries_ = std::move(working.force);

    return GlobalStructuralSystem{std::move(stiffness), std::move(force),
                                  AssemblySource{.body = model.body(),
                                                 .control = model.control(),
                                                 .geometry = model.geometryRevision(),
                                                 .mesh = numbering.map->stamp(),
                                                 .material = material.id(),
                                                 .materialRevision = material.revision()},
                                  std::move(*numbering.map), std::move(working.elements)};
}

std::optional<AssemblyProblem> structuralAssemblyProblem(const StructuralModel& model,
                                                         const StructuralMaterial& material,
                                                         const PreparedLoads& loads) {
    Numbering numbering = numberingOf(model.mesh().mesh());
    if (numbering.problem.has_value()) {
        return numbering.problem;
    }
    return run(model, material, loads, *numbering.map, *numbering.equations).problem;
}

} // namespace bettercad::structural
