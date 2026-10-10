// Canonical restraints and their constrained DOF set (P17-BC-001).
//
// THIS FILE ASKS; IT DOES NOT DECIDE. P16 decides which facets belong to a
// face and which nodes those facets carry; P17-DOF decides what index a node's
// component has and what a constraint set is. What is here is the join:
// restraint intent -> node set -> component mask -> indices, and the refusals
// along the way.
//
// NO DEDUPLICATION AND NO NUMBERING ARE WRITTEN HERE. boundaryNodesOf already
// sorts and uniques, and MeshDofMap::indexOf is the only arithmetic from a node
// to an index. A second copy of either would be a second definition.
//
// NO UNORDERED CONTAINER. The node union is a sorted vector and the constraint
// set is P17-DOF's, which sorts; so the result is identical in every preset and
// independent of the order the restraints were given in.

#include <bettercad/structural/StructuralConstraints.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::structural {

std::string toString(const RestraintComponents& components) {
    if (components.isEmpty()) {
        return "none";
    }
    if (components.isFixed()) {
        return "fixed";
    }
    std::string text;
    for (const DofComponent component : kDofComponents) {
        if (!components.holds(component)) {
            continue;
        }
        if (!text.empty()) {
            text += "+";
        }
        text += toString(component);
    }
    return text;
}

Result<void> validate(const StructuralRestraint& restraint) {
    if (!restraint.id().isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a restraint needs a valid identity");
    }
    // CORE'S OWN SELECTOR CHECK, NOT A SECOND OPINION. P16's
    // `boundaryFacetsOf` applies the same one when the restraint is resolved;
    // asking it here is what makes `validate` answerable without a mesh.
    if (Result<void> selector = bettercad::validate(restraint.face().face);
        !selector.has_value()) {
        return selector;
    }
    if (restraint.components().isEmpty()) {
        return makeError(
            ErrorCode::InvalidArgument,
            std::format("{} holds no displacement component at zero, so there is nothing for it "
                        "to constrain",
                        restraint.id()));
    }
    return {};
}

std::string_view toString(RestraintProblem problem) noexcept {
    switch (problem) {
    case RestraintProblem::DuplicateRestraintId:
        return "duplicate_restraint_id";
    case RestraintProblem::NoComponents:
        return "no_components";
    case RestraintProblem::TargetInvalid:
        return "target_invalid";
    case RestraintProblem::TargetUnresolved:
        return "target_unresolved";
    case RestraintProblem::TargetWithoutFacets:
        return "target_without_facets";
    case RestraintProblem::TargetWithoutNodes:
        return "target_without_nodes";
    case RestraintProblem::NumberingIsForADifferentMesh:
        return "numbering_is_for_a_different_mesh";
    }
    return "unknown";
}

namespace {

/// One pass, shared by `prepareStructuralRestraints` and
/// `structuralRestraintProblem` so the two cannot drift apart.
struct Prepared {
    std::optional<RestraintProblem> problem{};
    Error error{};
    std::vector<NodalDof> prescribed{};
    std::vector<RestraintResolution> resolutions{};
    std::vector<meshing::NodeId> nodes{};
};

[[nodiscard]] Prepared fail(RestraintProblem problem, Error error) {
    return Prepared{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Prepared run(const StructuralModel& model, const MeshDofMap& numbering,
                           std::span<const StructuralRestraint> restraints) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    const meshing::GeometryMeshMap& map = model.map();

    // THE NUMBERING MUST BE THIS MESH'S. Without it every index below would
    // mean something else, and the check is by stamp AND node count for the
    // reason MeshDofMap::describes records.
    if (!numbering.describes(mesh)) {
        return fail(RestraintProblem::NumberingIsForADifferentMesh,
                    makeError(ErrorCode::FailedPrecondition,
                              "the degree-of-freedom numbering was built for a different mesh "
                              "from the one this analysis holds, so its indices would name "
                              "unrelated material")
                        .error());
    }

    // THEN IDENTITY, before any resolution: two records for one RestraintId
    // means one would be ignored, and nothing below could tell which.
    std::vector<RestraintId> seen;
    seen.reserve(restraints.size());
    for (const StructuralRestraint& restraint : restraints) {
        if (std::ranges::find(seen, restraint.id()) != seen.end()) {
            return fail(RestraintProblem::DuplicateRestraintId,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} appears more than once, so one record would "
                                              "be ignored",
                                              restraint.id()))
                            .error());
        }
        seen.push_back(restraint.id());
    }

    // THEN THE COMPONENT MASK, before any mesh work: a restraint that holds
    // nothing at zero cannot be acted on, and resolving its face first would
    // report the wrong thing about it. The mask is checked here rather than
    // through `validate(restraint)` so each refusal maps onto exactly one
    // RestraintProblem -- a selector fault is reported by the resolution below,
    // in P16's own words.
    for (const StructuralRestraint& restraint : restraints) {
        if (restraint.components().isEmpty()) {
            return fail(RestraintProblem::NoComponents,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} holds no displacement component at zero, so "
                                              "there is nothing for it to constrain",
                                              restraint.id()))
                            .error());
        }
    }

    Prepared out;
    out.resolutions.reserve(restraints.size());

    for (const StructuralRestraint& restraint : restraints) {
        // P16 DECIDES THE FACETS. The three checks are the shared
        // structural::resolveFaceTarget's, so a load and a restraint cannot
        // disagree about what resolving a face means.
        //
        // RESOLVED ONCE ON THE PATH THAT SUCCEEDS. `faceTargetProblem` is
        // asked only when the resolution has already failed, which is what it
        // is for: it classifies, and `resolveFaceTarget` carries the facets
        // and P16's own message. An earlier draft asked both unconditionally
        // and copied every facet list twice.
        Result<std::vector<meshing::ElementId>> facets = resolveFaceTarget(map, restraint.face());
        if (!facets.has_value()) {
            const std::optional<TargetProblem> problem = faceTargetProblem(map, restraint.face());
            // The two functions are one pass over the same inputs, so a failed
            // resolution always has a problem. `SelectorInvalid` is the
            // fallback rather than a dereference, because a diagnostic path
            // must not be the thing that crashes.
            switch (problem.value_or(TargetProblem::SelectorInvalid)) {
            case TargetProblem::SelectorInvalid:
                // P16's OWN WORDS, because the selector is malformed on its own
                // terms and core's validate() has already said how.
                return fail(RestraintProblem::TargetInvalid, facets.error());
            case TargetProblem::Unresolved:
                return fail(RestraintProblem::TargetUnresolved,
                            makeError(ErrorCode::NotFound,
                                      std::format("{}: its target face names no face of the body "
                                                  "as it is now, so there is nothing to "
                                                  "constrain. Nothing nearby is rebound",
                                                  restraint.id()))
                                .error());
            case TargetProblem::WithoutFacets:
                return fail(RestraintProblem::TargetWithoutFacets,
                            makeError(ErrorCode::FailedPrecondition,
                                      std::format("{}: its target face resolved but the mesh "
                                                  "attributed no boundary facet to it",
                                                  restraint.id()))
                                .error());
            }
        }

        // P16 DECIDES THE NODES, and ALREADY sorts and deduplicates them:
        // boundaryNodesOf ends with sort() then unique(), so "every node of
        // every mapped facet, once, in a deterministic order" is this one call.
        // It also re-checks the map against the mesh.
        Result<std::vector<meshing::NodeId>> nodes =
            meshing::boundaryNodesOf(map, mesh, *facets);
        if (!nodes.has_value()) {
            return fail(RestraintProblem::TargetWithoutNodes, nodes.error());
        }
        if (nodes->empty()) {
            return fail(RestraintProblem::TargetWithoutNodes,
                        makeError(ErrorCode::FailedPrecondition,
                                  std::format("{}: its target resolved to {} boundary facets and "
                                              "those to no node, so it would constrain nothing",
                                              restraint.id(), facets->size()))
                            .error());
        }

        // NODE + COMPONENT -> INDEX IS P17-DOF'S. There is no 3 * nodeId here.
        //
        // AND THE INDEX IS KEPT (P17-REACTION-001, ADR-041). It was already
        // computed and verified here and then discarded; retaining it per
        // restraint is what lets a reaction be attributed to the restraint
        // that caused it without re-resolving the target after the solve.
        std::vector<DofIndex> mine;
        mine.reserve(nodes->size() * restraint.components().count());
        for (const meshing::NodeId node : *nodes) {
            for (const DofComponent component : kDofComponents) {
                if (!restraint.components().holds(component)) {
                    continue;
                }
                // indexOf refuses a node the numbering does not have. The
                // binding check above makes that unreachable for a matching
                // pair -- boundaryNodesOf returns nodes of this very mesh --
                // and it is propagated rather than skipped, because silently
                // dropping a node would leave the model under-constrained with
                // nothing reporting it.
                Result<DofIndex> index =
                    numbering.indexOf(NodalDof{.node = node, .component = component});
                if (!index.has_value()) {
                    return fail(RestraintProblem::NumberingIsForADifferentMesh, index.error());
                }
                mine.push_back(*index);
                out.prescribed.push_back(NodalDof{.node = node, .component = component});
            }
            out.nodes.push_back(node);
        }

        // ASCENDING AND UNIQUE WITHIN THE RESTRAINT. `nodes` is already
        // ascending and the components are taken in `kDofComponents` order,
        // and ADR-037's numbering is interleaved per node, so this is already
        // sorted -- the sort is here so the CONTRACT does not depend on that
        // reasoning holding after a future renumbering, and the unique is here
        // because a duplicate would silently double a reaction.
        std::ranges::sort(mine);
        mine.erase(std::ranges::unique(mine).begin(), mine.end());

        const std::size_t mineCount = mine.size();
        out.resolutions.push_back(
            RestraintResolution{.restraint = restraint.id(),
                                .facets = facets->size(),
                                .nodes = nodes->size(),
                                .components = restraint.components(),
                                .degreesOfFreedom = mineCount,
                                .constrained = std::move(mine)});
    }

    // THE UNION, DETERMINISTICALLY. Two restraints that reach one node
    // contribute it once -- overlapping faces share edge nodes, and that
    // overlap is lawful rather than an error.
    std::ranges::sort(out.nodes);
    out.nodes.erase(std::ranges::unique(out.nodes).begin(), out.nodes.end());
    std::ranges::sort(out.prescribed);
    out.prescribed.erase(std::ranges::unique(out.prescribed).begin(), out.prescribed.end());
    return out;
}

} // namespace

Result<PreparedRestraints> prepareStructuralRestraints(
    const StructuralModel& model, const MeshDofMap& numbering,
    std::span<const StructuralRestraint> restraints) {
    Prepared prepared = run(model, numbering, restraints);
    if (prepared.problem.has_value()) {
        return std::unexpected(std::move(prepared.error));
    }

    // P17-DOF OWNS WHAT A CONSTRAINT SET IS. The duplicates were removed above
    // because an overlap between two CAD faces is lawful and buildConstraintSet
    // refuses a repeated degree of freedom -- deliberately, since it cannot
    // know whether the repetition is harmless. Here it demonstrably is: every
    // prescribed value in this scope is zero.
    Result<ConstraintSet> constraints = buildConstraintSet(numbering, prepared.prescribed);
    if (!constraints.has_value()) {
        return std::unexpected(constraints.error());
    }

    // PUBLISHED IN ONE STEP, through the one private constructor. Nothing above
    // this line has touched a PreparedRestraints, so a refused set cannot leave
    // a partially built one behind.
    return PreparedRestraints{model.mesh().mesh().stamp(), model.mesh().mesh().nodes().size(),
                              std::move(*constraints), std::move(prepared.resolutions),
                              std::move(prepared.nodes)};
}

std::optional<RestraintProblem> structuralRestraintProblem(
    const StructuralModel& model, const MeshDofMap& numbering,
    std::span<const StructuralRestraint> restraints) {
    return run(model, numbering, restraints).problem;
}

} // namespace bettercad::structural
