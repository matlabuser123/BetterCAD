// Support reactions and static equilibrium (P17-REACTION-001).
//
// THE REACTION IS READ, NOT RECOMPUTED. `SolvedSystem::fullResidual()` is
// already `K u - F` over the full system, with every entry checked finite by
// the solve. This file selects its CONSTRAINED entries and groups them; there
// is no second `K u` product here, and a grep for `stiffness()` in this file
// finds nothing.
//
// NO LOAD IS RE-INTEGRATED. The external total comes from the assembled `F`
// that was solved, read through the qualified numbering. There is no pressure,
// traction or gravity arithmetic anywhere in this file.
//
// ONE CROSS PRODUCT, AND IT IS CORE'S. Every moment goes through
// `momentOf(lever, force)` = `lever x force`, whose signature forces the
// caller to form `x - O` and so makes the origin explicit. A grep for a
// hand-written cross product in this file finds nothing.
//
// EVERY SUM IS TAKEN IN A FROZEN ORDER: nodes ascending by `NodeId`,
// restraints in the order they were prepared, degrees of freedom ascending by
// `DofIndex`. No unordered container appears in a numerically significant
// traversal -- the multiplicity map below is keyed by `DofIndex` in a SORTED
// vector, not a hash table, for exactly that reason.

#include <bettercad/structural/StructuralReaction.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::structural {

std::string_view toString(ReactionProblem problem) noexcept {
    switch (problem) {
    case ReactionProblem::InvalidTolerance:
        return "invalid_tolerance";
    case ReactionProblem::SolutionSourceMismatch:
        return "solution_source_mismatch";
    case ReactionProblem::MeshMismatch:
        return "mesh_mismatch";
    case ReactionProblem::ConstraintSourceMismatch:
        return "constraint_source_mismatch";
    case ReactionProblem::DegreeOfFreedomOutOfRange:
        return "degree_of_freedom_out_of_range";
    case ReactionProblem::NodeMissing:
        return "node_missing";
    case ReactionProblem::NonFiniteReaction:
        return "non_finite_reaction";
    case ReactionProblem::NonFiniteExternal:
        return "non_finite_external";
    case ReactionProblem::NonFiniteBalance:
        return "non_finite_balance";
    case ReactionProblem::ForceImbalance:
        return "force_imbalance";
    case ReactionProblem::MomentImbalance:
        return "moment_imbalance";
    }
    return "unknown";
}

bool isFinite(const SupportReaction& reaction) noexcept {
    return isFinite(reaction.force);
}

Result<void> validate(const EquilibriumTolerance& tolerance) {
    // A THRESHOLD IS A RATIO AND A FLOOR IS A QUANTITY, and both must be
    // usable. A negative threshold refuses everything and a non-finite one
    // accepts everything, which are the two ways a gate stops being a gate.
    const auto check = [](double value, std::string_view what) -> Result<void> {
        if (!std::isfinite(value) || value <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the {} must be finite and positive, not {}", what,
                                         value));
        }
        return {};
    };
    if (Result<void> ok = check(tolerance.force, "force equilibrium threshold"); !ok.has_value()) {
        return ok;
    }
    if (Result<void> ok = check(tolerance.moment, "moment equilibrium threshold");
        !ok.has_value()) {
        return ok;
    }
    if (Result<void> ok = check(tolerance.forceFloor.si(), "force floor"); !ok.has_value()) {
        return ok;
    }
    return check(tolerance.momentFloor.si(), "moment floor");
}

// ---------------------------------------------------------------------------
// Magnitudes
// ---------------------------------------------------------------------------

namespace {

/// `std::hypot`, which is what the rest of the tree uses for a 3-vector norm
/// and does not overflow forming an intermediate square.
[[nodiscard]] double magnitude(const Force3D& f) noexcept {
    return std::hypot(f.x.si(), f.y.si(), f.z.si());
}

[[nodiscard]] double magnitude(const Moment3D& m) noexcept {
    return std::hypot(m.x.si(), m.y.si(), m.z.si());
}

[[nodiscard]] double largestComponent(const Force3D& f) noexcept {
    return std::max({std::abs(f.x.si()), std::abs(f.y.si()), std::abs(f.z.si())});
}

[[nodiscard]] double largestComponent(const Moment3D& m) noexcept {
    return std::max({std::abs(m.x.si()), std::abs(m.y.si()), std::abs(m.z.si())});
}

} // namespace

// ---------------------------------------------------------------------------
// The assembled external load, read back through the numbering
// ---------------------------------------------------------------------------

Result<Force3D> assembledForceResultant(const meshing::Mesh& mesh, const MeshDofMap& numbering,
                                        const ForceVector& force) {
    if (!numbering.describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the numbering was built for a different mesh, so its indices would "
                         "read unrelated rows of the force vector");
    }
    Force3D total{};
    // ASCENDING BY NodeId, which is the mesh's own enumeration and what makes
    // the floating-point sum reproducible.
    for (const meshing::Node& node : mesh.nodes()) {
        Result<std::array<DofIndex, kDofsPerNode>> indices = numbering.indicesOf(node.id);
        if (!indices.has_value()) {
            return std::unexpected(indices.error());
        }
        static_assert(kDofsPerNode == 3);
        static_assert(offsetOf(DofComponent::Ux) == 0);
        static_assert(offsetOf(DofComponent::Uy) == 1);
        static_assert(offsetOf(DofComponent::Uz) == 2);
        for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
            const auto row = static_cast<std::size_t>((*indices)[offset].value() - 1);
            if (row >= force.size()) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("{} names row {}, which is outside the assembled "
                                             "force vector of {} entries",
                                             node.id, row, force.size()));
            }
        }
        total = total + Force3D{force[static_cast<std::size_t>((*indices)[0].value() - 1)],
                                force[static_cast<std::size_t>((*indices)[1].value() - 1)],
                                force[static_cast<std::size_t>((*indices)[2].value() - 1)]};
    }
    if (!isFinite(total)) {
        return makeError(ErrorCode::Internal,
                         "the assembled external force resultant is not finite");
    }
    return total;
}

Result<Moment3D> assembledMomentResultant(const meshing::Mesh& mesh, const MeshDofMap& numbering,
                                          const ForceVector& force, const Point3D& origin) {
    if (!numbering.describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the numbering was built for a different mesh, so its indices would "
                         "read unrelated rows of the force vector");
    }
    Moment3D total{};
    // EVERY NODE, not only the boundary: a direct nodal load may act at an
    // interior node, and a load at a CONSTRAINED node is still external.
    for (const meshing::Node& node : mesh.nodes()) {
        Result<std::array<DofIndex, kDofsPerNode>> indices = numbering.indicesOf(node.id);
        if (!indices.has_value()) {
            return std::unexpected(indices.error());
        }
        for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
            if (static_cast<std::size_t>((*indices)[offset].value() - 1) >= force.size()) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("{} names a row outside the assembled force vector",
                                             node.id));
            }
        }
        const Force3D nodal{force[static_cast<std::size_t>((*indices)[0].value() - 1)],
                            force[static_cast<std::size_t>((*indices)[1].value() - 1)],
                            force[static_cast<std::size_t>((*indices)[2].value() - 1)]};
        // THE LEVER IS x - O, AND CORE'S momentOf TAKES IT RELATIVE for that
        // reason: `momentOf(lever, force)` is `lever x force`, so the operand
        // order is fixed by the signature and cannot be reversed here.
        const Translation3D lever{node.position.x - origin.x, node.position.y - origin.y,
                                  node.position.z - origin.z};
        total = total + momentOf(lever, nodal);
    }
    if (!isFinite(total)) {
        return makeError(ErrorCode::Internal,
                         "the assembled external moment resultant is not finite");
    }
    return total;
}

// ---------------------------------------------------------------------------
// Recovery
// ---------------------------------------------------------------------------

namespace {

/// The working state of one recovery, and the only place a partial set exists.
struct Working {
    std::optional<ReactionProblem> problem{};
    Error error{};

    std::size_t nodeCount = 0;
    std::size_t constrainedDofs = 0;
    std::size_t sharedDofs = 0;
    Force3D sharedForce{};
    Moment3D sharedMoment{};
    ForceBalance force{};
    MomentBalance moment{};
    std::vector<SupportReaction> nodal{};
    std::vector<RestraintReaction> restraints{};
};

[[nodiscard]] Working fail(ReactionProblem problem, Error error) {
    return Working{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Working fail(ReactionProblem problem, std::unexpected<Error> error) {
    return Working{.problem = problem, .error = std::move(error.error())};
}

/// How many restraints reached each constrained degree of freedom.
///
/// A SORTED VECTOR, NOT A HASH MAP. The multiplicities decide which reactions
/// are "owned" and which are "shared", and those two buckets are summed -- so
/// a hash table's traversal order would reach the floating-point result. The
/// lookup is a binary search over `ConstraintSet::constrained()`, which is
/// ascending and unique by construction.
[[nodiscard]] std::vector<std::size_t> multiplicities(const ConstraintSet& constraints,
                                                      std::span<const RestraintResolution> resolved) {
    const std::span<const DofIndex> all = constraints.constrained();
    std::vector<std::size_t> counts(all.size(), 0);
    for (const RestraintResolution& resolution : resolved) {
        for (const DofIndex dof : resolution.constrained) {
            const auto found = std::ranges::lower_bound(all, dof);
            if (found != all.end() && *found == dof) {
                ++counts[static_cast<std::size_t>(found - all.begin())];
            }
        }
    }
    return counts;
}

/// Completes the norms and the normalized ratio of a force balance.
void finish(ForceBalance& balance, Force floor) {
    balance.imbalance = balance.external + balance.reaction;
    balance.euclideanNorm = Force::fromSi(magnitude(balance.imbalance));
    balance.infinityNorm = Force::fromSi(largestComponent(balance.imbalance));
    // THE SCALE IS A SUM OF MAGNITUDES and is already accumulated by the
    // caller. Below the floor the system is unloaded and the honest ratio is
    // zero rather than a division.
    balance.normalized = balance.scale > floor
                             ? balance.euclideanNorm.si() / balance.scale.si()
                             : (balance.euclideanNorm.si() == 0.0
                                    ? 0.0
                                    : balance.euclideanNorm.si() / floor.si());
}

void finish(MomentBalance& balance, Torque floor) {
    balance.imbalance = balance.external + balance.reaction;
    balance.euclideanNorm = Torque::fromSi(magnitude(balance.imbalance));
    balance.infinityNorm = Torque::fromSi(largestComponent(balance.imbalance));
    balance.normalized = balance.scale > floor
                             ? balance.euclideanNorm.si() / balance.scale.si()
                             : (balance.euclideanNorm.si() == 0.0
                                    ? 0.0
                                    : balance.euclideanNorm.si() / floor.si());
}

/// One recovery, shared by `recoverSupportReactions` and `reactionProblem`.
[[nodiscard]] Working run(const StructuralModel& model, const GlobalStructuralSystem& system,
                          const PreparedRestraints& restraints, const PreparedLoads& loads,
                          const SolvedSystem& solution, const Point3D& origin,
                          const EquilibriumTolerance& tolerance) {
    if (Result<void> checked = validate(tolerance); !checked.has_value()) {
        return fail(ReactionProblem::InvalidTolerance, checked.error());
    }

    const meshing::Mesh& mesh = model.mesh().mesh();
    const MeshDofMap& numbering = system.numbering();

    // -----------------------------------------------------------------------
    // SOURCE COMPATIBILITY, BEFORE ANY ARITHMETIC.
    // -----------------------------------------------------------------------
    if (!(solution.source() == system.source())) {
        return fail(ReactionProblem::SolutionSourceMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              "the solved displacement field was produced from a different "
                              "assembled system than the one handed in, so its residual is not "
                              "this system's reaction"));
    }
    if (!numbering.describes(mesh) || !solution.describes(mesh) || !restraints.describes(mesh) ||
        !loads.describes(mesh)) {
        return fail(ReactionProblem::MeshMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              "the numbering, the solution, the prepared restraints and the "
                              "prepared loads must all describe the mesh this analysis holds"));
    }

    // THE CONSTRAINED SET MUST BE THE ONE THE SOLVE REDUCED WITH. Describing
    // the same mesh is not enough: a second preparation of the same restraints
    // would describe the mesh and could still differ, and attributing
    // reactions through a mapping that was not the solve's is exactly how a
    // source mismatch hides (ADR-041).
    const ConstraintSet& constraints = restraints.constraints();
    if (constraints.size() != solution.constrainedDegreesOfFreedom()) {
        return fail(ReactionProblem::ConstraintSourceMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("the prepared restraints constrain {} degrees of "
                                          "freedom and the solve reduced {}, so they are not the "
                                          "same constraint set",
                                          constraints.size(),
                                          solution.constrainedDegreesOfFreedom())));
    }

    const std::span<const double> residual = solution.fullResidual();
    const std::span<const DofIndex> constrained = constraints.constrained();

    Working out;
    out.nodeCount = mesh.nodes().size();
    out.constrainedDofs = constrained.size();
    out.moment.origin = origin;

    // -----------------------------------------------------------------------
    // THE REACTIONS: the CONSTRAINED entries of `K u - F`, grouped by node.
    // -----------------------------------------------------------------------
    const std::vector<std::size_t> counts = multiplicities(constraints, restraints.resolutions());

    // Ascending by DofIndex, which ADR-037 makes interleaved per node -- so one
    // node's three components are adjacent and the grouping below needs no
    // sort of its own.
    for (std::size_t position = 0; position < constrained.size(); ++position) {
        const DofIndex dof = constrained[position];
        const auto row = static_cast<std::size_t>(dof.value() - 1);
        if (!dof.isValid() || row >= residual.size()) {
            return fail(ReactionProblem::DegreeOfFreedomOutOfRange,
                        makeError(ErrorCode::FailedPrecondition,
                                  std::format("a constrained degree of freedom names row {}, "
                                              "which is outside the solved system of {} entries",
                                              row, residual.size())));
        }
        Result<NodalDof> named = numbering.dofAt(dof);
        if (!named.has_value()) {
            return fail(ReactionProblem::NodeMissing, named.error());
        }
        const double value = residual[row];
        if (!std::isfinite(value)) {
            return fail(ReactionProblem::NonFiniteReaction,
                        makeError(ErrorCode::Internal,
                                  std::format("the reaction at {} component {} is not finite",
                                              named->node, toString(named->component))));
        }

        // ONE ENTRY PER NODE. A node appears once however many of its three
        // degrees of freedom are constrained, so no component can be added
        // twice -- which a second loop over nodes would have risked.
        if (out.nodal.empty() || out.nodal.back().node != named->node) {
            out.nodal.push_back(SupportReaction{.node = named->node});
        }
        SupportReaction& entry = out.nodal.back();
        switch (named->component) {
        case DofComponent::Ux:
            entry.force.x = Force::fromSi(value);
            break;
        case DofComponent::Uy:
            entry.force.y = Force::fromSi(value);
            break;
        case DofComponent::Uz:
            entry.force.z = Force::fromSi(value);
            break;
        }
        // THE MASK IS BUILT FROM WHAT IS CONSTRAINED, so a component no
        // restraint reached is never set and its force stays exactly zero --
        // no free-residual value can reach it.
        entry.constrained.add(named->component);

        if (counts[position] >= 2) {
            ++out.sharedDofs;
        }
    }

    // THE GLOBAL TOTAL, over unique nodes in ascending order. Each constrained
    // degree of freedom contributed to exactly one node entry, so a shared
    // degree of freedom is counted ONCE however many restraints reached it.
    Force3D reactionTotal{};
    Moment3D reactionMoment{};
    double reactionScale = 0.0;
    double reactionMomentScale = 0.0;
    for (const SupportReaction& entry : out.nodal) {
        const meshing::Node* node = mesh.findNode(entry.node);
        if (node == nullptr) {
            return fail(ReactionProblem::NodeMissing,
                        makeError(ErrorCode::NotFound,
                                  std::format("{} is constrained but is not a node of this mesh",
                                              entry.node)));
        }
        reactionTotal = reactionTotal + entry.force;
        reactionScale += magnitude(entry.force);
        const Translation3D lever{node->position.x - origin.x, node->position.y - origin.y,
                                  node->position.z - origin.z};
        const Moment3D contribution = momentOf(lever, entry.force);
        reactionMoment = reactionMoment + contribution;
        reactionMomentScale += magnitude(contribution);
    }
    if (!isFinite(reactionTotal) || !isFinite(reactionMoment)) {
        return fail(ReactionProblem::NonFiniteReaction,
                    makeError(ErrorCode::Internal,
                              "a reaction resultant is not finite although its components are"));
    }

    // -----------------------------------------------------------------------
    // THE EXTERNAL LOAD: the assembled `F` THAT WAS SOLVED. No load is
    // re-integrated, and `loads` is never added to this -- it is the
    // independent oracle, checked by the tests rather than summed here.
    // -----------------------------------------------------------------------
    Result<Force3D> externalForce =
        assembledForceResultant(mesh, numbering, system.force());
    if (!externalForce.has_value()) {
        return fail(ReactionProblem::NonFiniteExternal, externalForce.error());
    }
    Result<Moment3D> externalMoment =
        assembledMomentResultant(mesh, numbering, system.force(), origin);
    if (!externalMoment.has_value()) {
        return fail(ReactionProblem::NonFiniteExternal, externalMoment.error());
    }

    // The external scale, over the same node order.
    double externalScale = 0.0;
    double externalMomentScale = 0.0;
    for (const meshing::Node& node : mesh.nodes()) {
        Result<std::array<DofIndex, kDofsPerNode>> indices = numbering.indicesOf(node.id);
        if (!indices.has_value()) {
            return fail(ReactionProblem::NodeMissing, indices.error());
        }
        const Force3D nodal{
            system.force()[static_cast<std::size_t>((*indices)[0].value() - 1)],
            system.force()[static_cast<std::size_t>((*indices)[1].value() - 1)],
            system.force()[static_cast<std::size_t>((*indices)[2].value() - 1)]};
        externalScale += magnitude(nodal);
        const Translation3D lever{node.position.x - origin.x, node.position.y - origin.y,
                                  node.position.z - origin.z};
        externalMomentScale += magnitude(momentOf(lever, nodal));
    }

    out.force.external = *externalForce;
    out.force.reaction = reactionTotal;
    out.force.scale = Force::fromSi(externalScale + reactionScale);
    finish(out.force, tolerance.forceFloor);

    out.moment.external = *externalMoment;
    out.moment.reaction = reactionMoment;
    out.moment.scale = Torque::fromSi(externalMomentScale + reactionMomentScale);
    finish(out.moment, tolerance.momentFloor);

    if (!std::isfinite(out.force.normalized) || !std::isfinite(out.moment.normalized) ||
        !isFinite(out.force.imbalance) || !isFinite(out.moment.imbalance)) {
        return fail(ReactionProblem::NonFiniteBalance,
                    makeError(ErrorCode::Internal,
                              "an equilibrium metric is not finite although its inputs are"));
    }

    // -----------------------------------------------------------------------
    // PER-RESTRAINT ATTRIBUTION, in the order the restraints were prepared.
    // -----------------------------------------------------------------------
    out.restraints.reserve(restraints.resolutions().size());
    for (const RestraintResolution& resolution : restraints.resolutions()) {
        RestraintReaction summary{};
        summary.restraint = resolution.restraint;
        // Ascending within the restraint, which `prepareStructuralRestraints`
        // guarantees -- so this sum is reproducible too.
        for (const DofIndex dof : resolution.constrained) {
            const auto found = std::ranges::lower_bound(constrained, dof);
            if (found == constrained.end() || *found != dof) {
                return fail(ReactionProblem::ConstraintSourceMismatch,
                            makeError(ErrorCode::FailedPrecondition,
                                      std::format("{} resolved to a degree of freedom the "
                                                  "constraint set does not contain, so the "
                                                  "restraints are not the solve's",
                                                  resolution.restraint)));
            }
            const auto position = static_cast<std::size_t>(found - constrained.begin());
            const auto row = static_cast<std::size_t>(dof.value() - 1);
            Result<NodalDof> named = numbering.dofAt(dof);
            if (!named.has_value()) {
                return fail(ReactionProblem::NodeMissing, named.error());
            }
            const meshing::Node* node = mesh.findNode(named->node);
            if (node == nullptr) {
                return fail(ReactionProblem::NodeMissing,
                            makeError(ErrorCode::NotFound,
                                      std::format("{} is not a node of this mesh", named->node)));
            }
            // ONE COMPONENT, which is what keeps a partially restrained node
            // from contributing a force it has no reaction for.
            Force3D single{};
            switch (named->component) {
            case DofComponent::Ux:
                single.x = Force::fromSi(residual[row]);
                break;
            case DofComponent::Uy:
                single.y = Force::fromSi(residual[row]);
                break;
            case DofComponent::Uz:
                single.z = Force::fromSi(residual[row]);
                break;
            }
            const Translation3D lever{node->position.x - origin.x, node->position.y - origin.y,
                                      node->position.z - origin.z};
            const Moment3D contribution = momentOf(lever, single);

            // OWNED means multiplicity one. The owned buckets are additive
            // across restraints by construction; the shared ones are not, and
            // are excluded from the identity (ADR-041).
            if (counts[position] == 1) {
                ++summary.ownedDegreesOfFreedom;
                summary.ownedForce = summary.ownedForce + single;
                summary.ownedMoment = summary.ownedMoment + contribution;
            } else {
                ++summary.sharedDegreesOfFreedom;
                summary.sharedForce = summary.sharedForce + single;
                summary.sharedMoment = summary.sharedMoment + contribution;
            }
        }
        out.restraints.push_back(summary);
    }

    // THE SHARED AGGREGATE, counted ONCE over the degrees of freedom more than
    // one restraint reached. With the owned buckets this reproduces the global
    // total exactly, which the tests assert as an identity.
    for (std::size_t position = 0; position < constrained.size(); ++position) {
        if (counts[position] < 2) {
            continue;
        }
        const DofIndex dof = constrained[position];
        const auto row = static_cast<std::size_t>(dof.value() - 1);
        Result<NodalDof> named = numbering.dofAt(dof);
        if (!named.has_value()) {
            return fail(ReactionProblem::NodeMissing, named.error());
        }
        const meshing::Node* node = mesh.findNode(named->node);
        if (node == nullptr) {
            return fail(ReactionProblem::NodeMissing,
                        makeError(ErrorCode::NotFound,
                                  std::format("{} is not a node of this mesh", named->node)));
        }
        Force3D single{};
        switch (named->component) {
        case DofComponent::Ux:
            single.x = Force::fromSi(residual[row]);
            break;
        case DofComponent::Uy:
            single.y = Force::fromSi(residual[row]);
            break;
        case DofComponent::Uz:
            single.z = Force::fromSi(residual[row]);
            break;
        }
        const Translation3D lever{node->position.x - origin.x, node->position.y - origin.y,
                                  node->position.z - origin.z};
        out.sharedForce = out.sharedForce + single;
        out.sharedMoment = out.sharedMoment + momentOf(lever, single);
    }

    // -----------------------------------------------------------------------
    // THE GATES. A FAILURE, NOT A WARNING: the solve can satisfy its own
    // residual gate and still have a reaction sign, mapping or load-accounting
    // defect, and these are the only checks that see it.
    // -----------------------------------------------------------------------
    if (!(out.force.normalized <= tolerance.force)) {
        return fail(ReactionProblem::ForceImbalance,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("force equilibrium failed: the normalized imbalance is "
                                          "{:.6e} against a threshold of {:.6e}. The imbalance is "
                                          "({:.6e}, {:.6e}, {:.6e}) N over a participating scale "
                                          "of {:.6e} N",
                                          out.force.normalized, tolerance.force,
                                          out.force.imbalance.x.si(), out.force.imbalance.y.si(),
                                          out.force.imbalance.z.si(), out.force.scale.si())));
    }
    // CHECKED EVEN THOUGH THE FORCE BALANCE PASSED. A pure couple balances in
    // force by construction and not in moment, so stopping here would miss it.
    if (!(out.moment.normalized <= tolerance.moment)) {
        return fail(ReactionProblem::MomentImbalance,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("moment equilibrium about ({:.6e}, {:.6e}, {:.6e}) m "
                                          "failed: the normalized imbalance is {:.6e} against a "
                                          "threshold of {:.6e}. The imbalance is ({:.6e}, {:.6e}, "
                                          "{:.6e}) N m over a participating scale of {:.6e} N m",
                                          origin.x.si(), origin.y.si(), origin.z.si(),
                                          out.moment.normalized, tolerance.moment,
                                          out.moment.imbalance.x.si(),
                                          out.moment.imbalance.y.si(),
                                          out.moment.imbalance.z.si(), out.moment.scale.si())));
    }

    return out;
}

} // namespace

Result<SupportReactions> recoverSupportReactions(const StructuralModel& model,
                                                 const GlobalStructuralSystem& system,
                                                 const PreparedRestraints& restraints,
                                                 const PreparedLoads& loads,
                                                 const SolvedSystem& solution,
                                                 const Point3D& origin,
                                                 const EquilibriumTolerance& tolerance) {
    Working work = run(model, system, restraints, loads, solution, origin, tolerance);
    if (work.problem.has_value()) {
        return std::unexpected(std::move(work.error));
    }
    // ATOMIC: the vectors move in only now, after both gates passed.
    return SupportReactions(system.source(), solution.mesh(), work.nodeCount,
                            work.constrainedDofs, work.sharedDofs, work.sharedForce,
                            work.sharedMoment, tolerance, work.force, work.moment,
                            std::move(work.nodal), std::move(work.restraints));
}

std::optional<ReactionProblem> reactionProblem(const StructuralModel& model,
                                               const GlobalStructuralSystem& system,
                                               const PreparedRestraints& restraints,
                                               const PreparedLoads& loads,
                                               const SolvedSystem& solution,
                                               const Point3D& origin,
                                               const EquilibriumTolerance& tolerance) {
    return run(model, system, restraints, loads, solution, origin, tolerance).problem;
}

Result<SupportReaction> SupportReactions::at(const meshing::Mesh& mesh,
                                             meshing::NodeId node) const {
    if (!describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "these reactions were recovered on a different mesh, so none of its "
                         "node handles names the same material");
    }
    const auto found = std::ranges::lower_bound(nodal_, node, {}, &SupportReaction::node);
    if (found == nodal_.end() || found->node != node) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a constrained node of the mesh these reactions "
                                     "were recovered on",
                                     node));
    }
    return *found;
}

Result<RestraintReaction> SupportReactions::of(RestraintId restraint) const {
    for (const RestraintReaction& summary : restraints_) {
        if (summary.restraint == restraint) {
            return summary;
        }
    }
    return makeError(ErrorCode::NotFound,
                     std::format("{} did not contribute to these reactions", restraint));
}

Result<Moment3D> SupportReactions::momentAbout(const meshing::Mesh& mesh,
                                               const Point3D& origin) const {
    if (!describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "these reactions were recovered on a different mesh, so its node "
                         "positions are not the ones they were computed from");
    }
    Moment3D total{};
    for (const SupportReaction& entry : nodal_) {
        const meshing::Node* node = mesh.findNode(entry.node);
        if (node == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} is not a node of this mesh", entry.node));
        }
        const Translation3D lever{node->position.x - origin.x, node->position.y - origin.y,
                                  node->position.z - origin.z};
        total = total + momentOf(lever, entry.force);
    }
    if (!isFinite(total)) {
        return makeError(ErrorCode::Internal, "the reaction moment is not finite");
    }
    return total;
}

} // namespace bettercad::structural
