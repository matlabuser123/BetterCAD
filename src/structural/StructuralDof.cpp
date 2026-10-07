// The degree-of-freedom numbering and the constraint model (P17-DOF-001).
//
// EVERY LOOKUP IS BY IDENTITY OVER ASCENDING STORAGE, never by indexing with a
// handle's value. That is P16's access model for the same reason -- node
// handles may be sparse -- and the arithmetic below converts between an
// ORDINAL and a DofIndex, never between a NodeId and a DofIndex.
//
// NO UNORDERED CONTAINER APPEARS IN THIS FILE. Determinism across presets is
// the milestone's gate, and an unordered_set of NodeIds would make the
// duplicate check's REPORTED duplicate depend on a hash's bucket order.

#include <bettercad/structural/StructuralDof.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::structural {
namespace {

/// The index of the degree of freedom at @p ordinal with component offset
/// @p offset. The numbering, in one place.
[[nodiscard]] DofIndex indexAt(std::size_t ordinal, std::size_t offset) noexcept {
    const DofIndex::ValueType base = static_cast<DofIndex::ValueType>(kDofsPerNode) *
                                     static_cast<DofIndex::ValueType>(ordinal);
    return DofIndex::fromValue(base + static_cast<DofIndex::ValueType>(offset) + 1);
}

} // namespace

std::string_view toString(DofMapProblem problem) noexcept {
    switch (problem) {
    case DofMapProblem::MeshHasNoNodes:
        return "mesh_has_no_nodes";
    case DofMapProblem::MeshHasNoIdentity:
        return "mesh_has_no_identity";
    }
    return "unknown";
}

std::string_view toString(ConstraintProblem problem) noexcept {
    switch (problem) {
    case ConstraintProblem::NodeNotInMesh:
        return "node_not_in_mesh";
    case ConstraintProblem::ComponentNotRecognised:
        return "component_not_recognised";
    case ConstraintProblem::DuplicateDof:
        return "duplicate_dof";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// MeshDofMap
// ---------------------------------------------------------------------------

std::optional<std::size_t> MeshDofMap::nodeOrdinal(meshing::NodeId node) const noexcept {
    if (!node.isValid()) {
        return std::nullopt;
    }
    const auto found = std::ranges::lower_bound(nodes_, node);
    if (found == nodes_.end() || *found != node) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - nodes_.begin());
}

Result<DofIndex> MeshDofMap::indexOf(const NodalDof& dof) const {
    // THE COMPONENT IS CHECKED FIRST, and the order matters for the
    // diagnostic rather than for correctness: a caller who built a NodalDof
    // from an out-of-range integer has a bug in their component, and reporting
    // a missing node would send them looking at the mesh.
    const std::size_t offset = offsetOf(dof.component);
    if (offset >= kDofsPerNode) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is not a displacement component of a solid mesh node",
                                     static_cast<unsigned>(dof.component)));
    }
    const std::optional<std::size_t> ordinal = nodeOrdinal(dof.node);
    if (!ordinal.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a node of the mesh this numbering describes",
                                     dof.node));
    }
    return indexAt(*ordinal, offset);
}

Result<std::array<DofIndex, kDofsPerNode>> MeshDofMap::indicesOf(meshing::NodeId node) const {
    const std::optional<std::size_t> ordinal = nodeOrdinal(node);
    if (!ordinal.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a node of the mesh this numbering describes",
                                     node));
    }
    std::array<DofIndex, kDofsPerNode> indices{};
    for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
        indices[offset] = indexAt(*ordinal, offset);
    }
    return indices;
}

Result<NodalDof> MeshDofMap::dofAt(DofIndex index) const {
    if (!contains(index)) {
        return makeError(ErrorCode::NotFound,
                         std::format("degree of freedom {} is outside the {} this numbering has",
                                     index.value(), dofCount()));
    }
    const DofIndex::ValueType zeroBased = index.value() - 1;
    const auto ordinal =
        static_cast<std::size_t>(zeroBased / static_cast<DofIndex::ValueType>(kDofsPerNode));
    const auto offset =
        static_cast<std::size_t>(zeroBased % static_cast<DofIndex::ValueType>(kDofsPerNode));
    return NodalDof{.node = nodes_[ordinal], .component = componentAt(offset)};
}

Result<MeshDofMap> buildMeshDofMap(const meshing::Mesh& mesh) {
    if (const std::optional<DofMapProblem> problem = dofMapProblem(mesh); problem.has_value()) {
        switch (*problem) {
        case DofMapProblem::MeshHasNoNodes:
            return makeError(ErrorCode::FailedPrecondition,
                             "a mesh with no nodes has no degrees of freedom to number");
        case DofMapProblem::MeshHasNoIdentity:
            return makeError(ErrorCode::FailedPrecondition,
                             "the mesh carries no identity, so a numbering built from it could "
                             "not be bound to it");
        }
    }

    MeshDofMap map;
    map.stamp_ = mesh.stamp();
    map.nodes_.reserve(mesh.nodes().size());
    // THE MESH'S OWN ENUMERATION, IN ITS OWN ORDER. Mesh guarantees it is
    // ascending by NodeId; copying it rather than sorting it is what makes the
    // ordinal here the same ordinal StructuralResult's displacement array uses,
    // because that one is also "parallel to mesh.nodes()". A sort would give
    // the same answer today and would be a second definition of the order.
    for (const meshing::Node& node : mesh.nodes()) {
        map.nodes_.push_back(node.id);
    }
    return map;
}

std::optional<DofMapProblem> dofMapProblem(const meshing::Mesh& mesh) noexcept {
    if (!mesh.stamp().isValid()) {
        return DofMapProblem::MeshHasNoIdentity;
    }
    if (mesh.nodes().empty()) {
        return DofMapProblem::MeshHasNoNodes;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ConstraintSet
// ---------------------------------------------------------------------------

bool ConstraintSet::contains(DofIndex index) const noexcept {
    return std::ranges::binary_search(constrained_, index);
}

namespace {

/// One pass, shared by `buildConstraintSet` and `constraintProblem` so the two
/// cannot drift apart: the same checks in the same order, reporting the same
/// first offender.
struct Validated {
    std::optional<ConstraintProblem> problem{};
    Error error{};
    std::vector<DofIndex> constrained{};
};

[[nodiscard]] Validated validate(const MeshDofMap& map, std::span<const NodalDof> prescribed) {
    Validated out;
    out.constrained.reserve(prescribed.size());
    for (const NodalDof& dof : prescribed) {
        Result<DofIndex> index = map.indexOf(dof);
        if (!index.has_value()) {
            out.problem = offsetOf(dof.component) >= kDofsPerNode
                              ? ConstraintProblem::ComponentNotRecognised
                              : ConstraintProblem::NodeNotInMesh;
            out.error = index.error();
            return out;
        }
        out.constrained.push_back(*index);
    }

    // SORTED, THEN CHECKED FOR REPEATS. Sorting first is what makes the result
    // independent of the order the caller listed its restraints in, and it
    // makes the duplicate check exact rather than quadratic. The input span is
    // not modified: the indices were copied out above.
    std::ranges::sort(out.constrained);
    const auto repeated = std::ranges::adjacent_find(out.constrained);
    if (repeated != out.constrained.end()) {
        const Result<NodalDof> dof = map.dofAt(*repeated);
        out.problem = ConstraintProblem::DuplicateDof;
        out.error =
            makeError(ErrorCode::InvalidArgument,
                      dof.has_value()
                          ? std::format("{} {} is constrained more than once", dof->node,
                                        toString(dof->component))
                          : std::format("degree of freedom {} is constrained more than once",
                                        repeated->value()))
                .error();
        out.constrained.clear();
        return out;
    }
    return out;
}

} // namespace

Result<ConstraintSet> buildConstraintSet(const MeshDofMap& map,
                                         std::span<const NodalDof> prescribed) {
    Validated validated = validate(map, prescribed);
    if (validated.problem.has_value()) {
        return std::unexpected(std::move(validated.error));
    }
    ConstraintSet set;
    set.stamp_ = map.stamp();
    set.constrained_ = std::move(validated.constrained);
    return set;
}

std::optional<ConstraintProblem> constraintProblem(const MeshDofMap& map,
                                                   std::span<const NodalDof> prescribed) {
    return validate(map, prescribed).problem;
}

std::vector<NodalDof> fullyFixedDofs(std::span<const meshing::NodeId> nodes) {
    std::vector<NodalDof> dofs;
    dofs.reserve(nodes.size() * kDofsPerNode);
    for (const meshing::NodeId node : nodes) {
        for (const DofComponent component : kDofComponents) {
            dofs.push_back(NodalDof{.node = node, .component = component});
        }
    }
    return dofs;
}

// ---------------------------------------------------------------------------
// FreeEquationMap
// ---------------------------------------------------------------------------

std::optional<FreeEquationIndex> FreeEquationMap::equationOf(DofIndex index) const noexcept {
    if (!contains(index)) {
        return std::nullopt;
    }
    const auto found = std::ranges::lower_bound(freeDofs_, index);
    if (found == freeDofs_.end() || *found != index) {
        return std::nullopt;
    }
    return FreeEquationIndex::fromValue(
        static_cast<FreeEquationIndex::ValueType>(found - freeDofs_.begin()));
}

Result<FreeEquationMap> buildFreeEquationMap(const MeshDofMap& map,
                                             const ConstraintSet& constraints) {
    // THE BINDING CHECK. This is where the numbering and the restraints meet,
    // so a constraint set built against a different mesh is refused once,
    // here, rather than being caught by a stamp comparison an assembly site
    // might omit.
    if (map.stamp() != constraints.stamp()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the constraint set was built against a different mesh from the "
                         "numbering, so its degree-of-freedom indices mean something else");
    }

    // AND THE RANGE, BECAUSE THE STAMP ALONE IS NOT SUFFICIENT. MeshBuilder
    // sets its stamp in its constructor and build() is a snapshot, so two
    // snapshots of one builder share a stamp and may hold different numbers of
    // nodes -- which means a set built against the larger can carry indices
    // this numbering does not have. Without this check the loop below simply
    // never meets them: every degree of freedom would come back FREE, and a
    // restrained model would be assembled as an unrestrained one. The set is
    // ascending, so one comparison against its last element settles it, and
    // it also makes the reservation below safe from an unsigned subtraction
    // below zero.
    const std::span<const DofIndex> constrained = constraints.constrained();
    if (!constrained.empty() && !map.contains(constrained.back())) {
        return makeError(
            ErrorCode::FailedPrecondition,
            std::format("the constraint set prescribes degree of freedom {}, which this "
                        "numbering of {} does not have",
                        constrained.back().value(), map.dofCount()));
    }

    FreeEquationMap equations;
    equations.stamp_ = map.stamp();
    equations.dofCount_ = map.dofCount();
    equations.freeDofs_.reserve(static_cast<std::size_t>(map.dofCount()) - constraints.size());
    // ASCENDING, SO THE EQUATION NUMBERING IS COMPACT AND DETERMINISTIC.
    // Walking the degrees of freedom in index order and the constraints in
    // their own ascending order means each is visited once, and the equation a
    // free degree of freedom receives depends on nothing but how many
    // constrained ones precede it.
    std::size_t next = 0;
    for (DofIndex::ValueType value = 1; value <= map.dofCount(); ++value) {
        const DofIndex index = DofIndex::fromValue(value);
        if (next < constrained.size() && constrained[next] == index) {
            ++next;
            continue;
        }
        equations.freeDofs_.push_back(index);
    }
    return equations;
}

} // namespace bettercad::structural
