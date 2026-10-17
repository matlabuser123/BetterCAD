// The temperature degree-of-freedom numbering and the prescribed-temperature
// constraint model (P18-DOF-001).
//
// EVERY LOOKUP IS BY IDENTITY OVER ASCENDING STORAGE, never by indexing with a
// handle's value. That is P16's access model for the same reason -- node
// handles may be sparse -- and the arithmetic below converts between an
// ORDINAL and a ThermalDofIndex, never between a NodeId and a ThermalDofIndex.
//
// NO UNORDERED CONTAINER APPEARS IN THIS FILE. Determinism across presets is
// the milestone's gate, and a hash of NodeIds or of constraints would make the
// REPORTED duplicate or conflict depend on a bucket order.

#include <bettercad/thermal/ThermalDof.hpp>

#include <bettercad/thermal/ThermalData.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::thermal {

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
    case ConstraintProblem::DofNotInMap:
        return "dof_not_in_map";
    case ConstraintProblem::TemperatureNotAbsolute:
        return "temperature_not_absolute";
    case ConstraintProblem::ConflictingTemperature:
        return "conflicting_temperature";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// ThermalDofMap
// ---------------------------------------------------------------------------

Result<ThermalDofIndex> ThermalDofMap::dofOf(meshing::NodeId node) const {
    if (node.isValid()) {
        const auto found = std::ranges::lower_bound(nodes_, node);
        if (found != nodes_.end() && *found == node) {
            return ThermalDofIndex::fromValue(
                static_cast<ThermalDofIndex::ValueType>(found - nodes_.begin()));
        }
    }
    return makeError(ErrorCode::NotFound,
                     std::format("{} is not a node of the mesh this numbering describes", node));
}

Result<meshing::NodeId> ThermalDofMap::nodeOf(ThermalDofIndex dof) const {
    if (!contains(dof)) {
        return makeError(ErrorCode::NotFound,
                         std::format("temperature unknown {} is outside the {} this numbering has",
                                     dof.value(), dofCount()));
    }
    return nodes_[static_cast<std::size_t>(dof.value())];
}

Result<std::array<ThermalDofIndex, 4>> ThermalDofMap::elementDofs(const meshing::Tetrahedron& element) const {
    // EVERY NODE RESOLVED BEFORE ANY IS RETURNED, so a tetrahedron with one
    // foreign node yields nothing rather than three unknowns and a lie.
    const Result<ThermalDofIndex> d0 = dofOf(element.nodes[0]);
    if (!d0.has_value()) {
        return std::unexpected(d0.error());
    }
    const Result<ThermalDofIndex> d1 = dofOf(element.nodes[1]);
    if (!d1.has_value()) {
        return std::unexpected(d1.error());
    }
    const Result<ThermalDofIndex> d2 = dofOf(element.nodes[2]);
    if (!d2.has_value()) {
        return std::unexpected(d2.error());
    }
    const Result<ThermalDofIndex> d3 = dofOf(element.nodes[3]);
    if (!d3.has_value()) {
        return std::unexpected(d3.error());
    }
    // THE ELEMENT'S OWN ORDER, EXACTLY. Not sorted: entry i is node i.
    return std::array<ThermalDofIndex, 4>{*d0, *d1, *d2, *d3};
}

Result<ThermalDofMap> buildThermalDofMap(const meshing::Mesh& mesh) {
    if (const std::optional<DofMapProblem> problem = thermalDofMapProblem(mesh); problem.has_value()) {
        switch (*problem) {
        case DofMapProblem::MeshHasNoNodes:
            return makeError(ErrorCode::FailedPrecondition,
                             "a mesh with no nodes has no temperature unknowns to number");
        case DofMapProblem::MeshHasNoIdentity:
            return makeError(ErrorCode::FailedPrecondition,
                             "the mesh carries no identity, so a numbering built from it could "
                             "not be bound to it");
        }
    }

    ThermalDofMap map;
    map.stamp_ = mesh.stamp();
    map.nodes_.reserve(mesh.nodes().size());
    // THE MESH'S OWN ENUMERATION, IN ITS OWN ORDER. Mesh guarantees it is
    // ascending by NodeId and MeshBuilder refuses a handle that is not greater
    // than the last; copying it rather than sorting it is what makes the
    // ordinal here the same ordinal ThermalResult's temperature array uses,
    // because that one is also "parallel to mesh.nodes()". The ascent is
    // verified while copying, so that a Mesh produced by anything other than
    // the builder -- which nothing in the tree does today -- could not hand
    // the binary search an unsorted list. Unreachable, so deliberately NOT
    // claimed as tested and NOT an enumerator of DofMapProblem.
    for (const meshing::Node& node : mesh.nodes()) {
        if (!map.nodes_.empty() && !(map.nodes_.back() < node.id)) {
            return makeError(ErrorCode::Internal,
                             std::format("the mesh's nodes are not strictly ascending at {}, which "
                                         "Mesh guarantees and MeshBuilder enforces",
                                         node.id));
        }
        map.nodes_.push_back(node.id);
    }
    return map;
}

std::optional<DofMapProblem> thermalDofMapProblem(const meshing::Mesh& mesh) noexcept {
    if (!mesh.stamp().isValid()) {
        return DofMapProblem::MeshHasNoIdentity;
    }
    if (mesh.nodes().empty()) {
        return DofMapProblem::MeshHasNoNodes;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ThermalConstraintMap
// ---------------------------------------------------------------------------

std::optional<Temperature> ThermalConstraintMap::prescribedTemperature(ThermalDofIndex dof) const noexcept {
    const auto found = std::ranges::lower_bound(
        constrained_, dof, {}, [](const ThermalTemperatureConstraint& c) { return c.dof; });
    if (found == constrained_.end() || found->dof != dof) {
        return std::nullopt;
    }
    return found->temperature;
}

namespace {

/// One pass, shared by `buildThermalConstraintMap` and
/// `thermalConstraintProblem` so the two cannot drift apart: the same checks
/// in the same order, reporting the same first offender.
struct ValidatedConstraints {
    std::optional<ConstraintProblem> problem{};
    Error error{};
    std::vector<ThermalTemperatureConstraint> constrained{};
};

[[nodiscard]] ValidatedConstraints validateConstraints(const ThermalDofMap& map,
                                                       std::span<const ThermalTemperatureConstraint> prescribed) {
    ValidatedConstraints out;
    out.constrained.reserve(prescribed.size());
    for (const ThermalTemperatureConstraint& constraint : prescribed) {
        if (!map.contains(constraint.dof)) {
            out.problem = ConstraintProblem::DofNotInMap;
            out.error = makeError(ErrorCode::NotFound,
                                  std::format("temperature unknown {} is outside the {} this "
                                              "numbering has",
                                              constraint.dof.value(), map.dofCount()))
                            .error();
            return out;
        }
        if (!isAbsoluteTemperature(constraint.temperature)) {
            out.problem = ConstraintProblem::TemperatureNotAbsolute;
            out.error = makeError(ErrorCode::InvalidArgument,
                                  std::format("temperature unknown {} is prescribed {} K, which is "
                                              "not a finite absolute temperature",
                                              constraint.dof.value(), constraint.temperature.si()))
                            .error();
            return out;
        }
        out.constrained.push_back(constraint);
    }

    // SORTED BY UNKNOWN, THEN BY VALUE, THEN CHECKED PAIRWISE. Sorting first
    // is what makes the result independent of the order the caller listed its
    // conditions in. Two adjacent entries on one unknown are either the SAME
    // temperature -- one constraint, stated twice, kept once -- or DIFFERENT
    // temperatures, which is a conflict and is refused with both values named.
    // Exact comparison of the kelvin values, deliberately: a prescribed
    // temperature is user intent and there is no tolerance under which two
    // different intents are one.
    std::ranges::sort(out.constrained, [](const ThermalTemperatureConstraint& a,
                                          const ThermalTemperatureConstraint& b) {
        if (a.dof != b.dof) {
            return a.dof < b.dof;
        }
        return a.temperature.si() < b.temperature.si();
    });
    std::vector<ThermalTemperatureConstraint> unique;
    unique.reserve(out.constrained.size());
    for (const ThermalTemperatureConstraint& constraint : out.constrained) {
        if (!unique.empty() && unique.back().dof == constraint.dof) {
            if (unique.back().temperature == constraint.temperature) {
                continue;
            }
            out.problem = ConstraintProblem::ConflictingTemperature;
            out.error = makeError(ErrorCode::InvalidArgument,
                                  std::format("temperature unknown {} is prescribed both {} K and "
                                              "{} K",
                                              constraint.dof.value(), unique.back().temperature.si(),
                                              constraint.temperature.si()))
                            .error();
            out.constrained.clear();
            return out;
        }
        unique.push_back(constraint);
    }
    out.constrained = std::move(unique);
    return out;
}

} // namespace

Result<ThermalConstraintMap> buildThermalConstraintMap(const ThermalDofMap& map,
                                                       std::span<const ThermalTemperatureConstraint> prescribed) {
    ValidatedConstraints validated = validateConstraints(map, prescribed);
    if (validated.problem.has_value()) {
        return std::unexpected(std::move(validated.error));
    }
    ThermalConstraintMap constraints;
    constraints.stamp_ = map.stamp();
    constraints.dofCount_ = map.dofCount();
    constraints.constrained_ = std::move(validated.constrained);
    return constraints;
}

std::optional<ConstraintProblem> thermalConstraintProblem(const ThermalDofMap& map,
                                                          std::span<const ThermalTemperatureConstraint> prescribed) {
    return validateConstraints(map, prescribed).problem;
}

// ---------------------------------------------------------------------------
// ThermalFreeEquationMap
// ---------------------------------------------------------------------------

std::optional<ThermalFreeEquationIndex> ThermalFreeEquationMap::equationOf(ThermalDofIndex dof) const noexcept {
    if (!contains(dof)) {
        return std::nullopt;
    }
    const auto found = std::ranges::lower_bound(freeDofs_, dof);
    if (found == freeDofs_.end() || *found != dof) {
        return std::nullopt;
    }
    return ThermalFreeEquationIndex::fromValue(
        static_cast<ThermalFreeEquationIndex::ValueType>(found - freeDofs_.begin()));
}

std::optional<ThermalDofIndex> ThermalFreeEquationMap::dofOf(ThermalFreeEquationIndex equation) const noexcept {
    if (equation.value() >= freeDofs_.size()) {
        return std::nullopt;
    }
    return freeDofs_[static_cast<std::size_t>(equation.value())];
}

bool ThermalFreeEquationMap::agreesWith(const ThermalDofMap& map) const noexcept {
    return stamp_.isValid() && stamp_ == map.stamp() && dofCount_ == map.dofCount();
}

bool ThermalFreeEquationMap::agreesWith(const ThermalConstraintMap& constraints) const noexcept {
    if (!stamp_.isValid() || stamp_ != constraints.stamp() || dofCount_ != constraints.dofCount()) {
        return false;
    }
    // THE COMPLEMENT, CHECKED RATHER THAN ASSUMED: the counts must close and
    // no prescribed unknown may hold a row. Together those make the free list
    // exactly the unknowns the constraint map leaves free.
    if (freeDofs_.size() + constraints.size() != static_cast<std::size_t>(dofCount_)) {
        return false;
    }
    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {
        if (std::ranges::binary_search(freeDofs_, constraint.dof)) {
            return false;
        }
    }
    return true;
}

Result<ThermalFreeEquationMap> buildThermalFreeEquationMap(const ThermalDofMap& map,
                                                           const ThermalConstraintMap& constraints) {
    // THE BINDING CHECK. This is where the numbering and the constraints
    // meet, so a constraint map built against a different mesh -- or a
    // different snapshot of the same builder, which shares the stamp and
    // differs in count -- is refused once, here.
    if (map.stamp() != constraints.stamp() || map.dofCount() != constraints.dofCount()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the constraint map was built against a different numbering, so its "
                         "temperature unknowns mean something else");
    }

    ThermalFreeEquationMap equations;
    equations.stamp_ = map.stamp();
    equations.dofCount_ = map.dofCount();
    equations.freeDofs_.reserve(static_cast<std::size_t>(map.dofCount()) - constraints.size());
    // ASCENDING, SO THE ROW NUMBERING IS COMPACT AND DETERMINISTIC. Walking
    // the unknowns in index order and the constraints in their own ascending
    // order means each is visited once, and the row a free unknown receives
    // depends on nothing but how many prescribed ones precede it.
    const std::span<const ThermalTemperatureConstraint> constrained = constraints.constrained();
    std::size_t next = 0;
    for (ThermalDofIndex::ValueType value = 0; value < map.dofCount(); ++value) {
        const ThermalDofIndex dof = ThermalDofIndex::fromValue(value);
        if (next < constrained.size() && constrained[next].dof == dof) {
            ++next;
            continue;
        }
        equations.freeDofs_.push_back(dof);
    }
    return equations;
}

Result<std::vector<Temperature>> reconstructTemperatures(const ThermalFreeEquationMap& equations,
                                                         const ThermalConstraintMap& constraints,
                                                         std::span<const Temperature> free) {
    if (!equations.agreesWith(constraints)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the free-equation map is not the complement of this constraint map, so "
                         "the two cannot be combined into one temperature vector");
    }
    if (free.size() != equations.freeCount()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} reduced temperatures for {} free unknowns", free.size(),
                                     equations.freeCount()));
    }
    std::vector<Temperature> full;
    full.reserve(static_cast<std::size_t>(equations.dofCount()));
    const std::span<const ThermalTemperatureConstraint> constrained = constraints.constrained();
    std::size_t nextConstrained = 0;
    std::size_t nextFree = 0;
    for (ThermalDofIndex::ValueType value = 0; value < equations.dofCount(); ++value) {
        const ThermalDofIndex dof = ThermalDofIndex::fromValue(value);
        if (nextConstrained < constrained.size() && constrained[nextConstrained].dof == dof) {
            // THE PRESCRIBED VALUE, NOT ZERO. The thermal Dirichlet condition
            // carries its temperature, and this is where it re-enters the
            // full vector.
            full.push_back(constrained[nextConstrained].temperature);
            ++nextConstrained;
        } else {
            full.push_back(free[nextFree]);
            ++nextFree;
        }
    }
    return full;
}

} // namespace bettercad::thermal
