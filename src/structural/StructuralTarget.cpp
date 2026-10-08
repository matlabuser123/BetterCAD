// Resolving a canonical CAD face target against the current mesh
// (P17-BC-001, extracted unchanged from P17-LOAD-001).
//
// THE THREE CHECKS ARE P16'S CONTRACT, in P16's order:
//
//   boundaryFacetsOf FAILS          only for a malformed selector. An
//                                   unresolvable reference is a STATUS it
//                                   reports, not an error -- which is why
//                                   Unresolved is read from fullyResolved()
//                                   and not from the Result
//   !fullyResolved()                the reference names no face of the body
//   facets.empty()                  it resolved and got no facet
//
// Nothing here classifies geometry.

#include <bettercad/structural/StructuralTarget.hpp>

namespace bettercad::structural {

std::string_view toString(TargetProblem problem) noexcept {
    switch (problem) {
    case TargetProblem::SelectorInvalid:
        return "selector_invalid";
    case TargetProblem::Unresolved:
        return "unresolved";
    case TargetProblem::WithoutFacets:
        return "without_facets";
    }
    return "unknown";
}

namespace {

/// One pass, so `resolveFaceTarget` and `faceTargetProblem` cannot drift.
struct Resolved {
    std::optional<TargetProblem> problem{};
    Error error{};
    std::vector<meshing::ElementId> facets{};
};

[[nodiscard]] Resolved resolve(const meshing::GeometryMeshMap& map, const FaceName& face) {
    Resolved out;
    Result<meshing::BoundaryFacetSet> set = meshing::boundaryFacetsOf(map, face);
    if (!set.has_value()) {
        out.problem = TargetProblem::SelectorInvalid;
        out.error = set.error();
        return out;
    }
    if (!set->fullyResolved()) {
        out.problem = TargetProblem::Unresolved;
        out.error = makeError(ErrorCode::NotFound,
                              "the target face names no face of the body as it is now, so "
                              "nothing nearby is substituted")
                        .error();
        return out;
    }
    if (set->facets.empty()) {
        out.problem = TargetProblem::WithoutFacets;
        out.error = makeError(ErrorCode::FailedPrecondition,
                              "the target face resolved but the mesh attributed no boundary "
                              "facet to it")
                        .error();
        return out;
    }
    out.facets = set->facets;
    return out;
}

} // namespace

Result<std::vector<meshing::ElementId>> resolveFaceTarget(const meshing::GeometryMeshMap& map,
                                                          const FaceName& face) {
    Resolved resolved = resolve(map, face);
    if (resolved.problem.has_value()) {
        return std::unexpected(std::move(resolved.error));
    }
    return std::move(resolved.facets);
}

std::optional<TargetProblem> faceTargetProblem(const meshing::GeometryMeshMap& map,
                                               const FaceName& face) {
    return resolve(map, face).problem;
}

} // namespace bettercad::structural
