#pragma once

// P19-BC-001 -- resolving a canonical CAD face target against the current mesh.
//
// WHY THIS FILE EXISTS, AND WHY IT IS A THIRD COPY. P19-BC-001 needs "a
// FaceName, resolved through P16's mapping, or the reason it could not be", and
// P19-VALID-001 will need the same three checks. `thermal::resolveFaceTarget`
// and `structural::resolveFaceTarget` are the same function, and `cfd/` may
// include neither: cfd is layer 42, BELOW thermal at 45 and structural at 50,
// so that a wall pressure and a wall heat-transfer coefficient can flow upward
// (ADR-059). So the three checks are written here, once, for the CFD consumers.
//
// THE PRECEDENT IS TO WRITE IT NOW, NOT TO AVOID THE COPY.
// `ThermalTarget.hpp` records what deferring cost: P17-LOAD-001 wrote the
// checks in an anonymous namespace, P17-BC-001 then needed them, and extracting
// them made `StructuralLoad.cpp` a changed production file that had to be
// requalified. A header of its own means P19-VALID-001 includes this and
// touches nothing qualified. If a FOURTH module ever needs these checks, the
// right move is a layer-0 home, not a fourth copy.
//
// THE THREE CHECKS ARE P16'S CONTRACT, in P16's order, and nothing here
// classifies geometry:
//
//   boundaryFacetsOf FAILS         only for a malformed selector. An
//                                  unresolvable reference is a STATUS it
//                                  reports, not an error, which is why
//                                  Unresolved is read from fullyResolved()
//   !fullyResolved()               the reference names no face of the body
//   facets.empty()                 it resolved and got no facet
//
// THREE AND NOT TWO. `BoundaryFacetSet::fullyResolved()` inspects `state` only,
// so a `FaceName` that lands on a real CAD face the mesher gave no facet comes
// back Resolved with an empty `facets`. Stopping at two checks would hand a
// consumer an empty facet list as a success.
//
// NO NEAREST FACE, NO DISTANCE TO A PLANE, NO NORMAL COMPARISON FOR MATCHING,
// NO CENTROID TEST. If P16 says a name resolves to nothing, it resolves to
// nothing. P19-BC-001 does compute an outward normal, but only to VERIFY a
// condition the user gave -- never to choose which face a condition belongs to.

#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

/// Why a canonical CAD face target cannot be resolved against a current mesh.
///
/// NEUTRAL BY DESIGN: every CFD boundary condition fails for the same three
/// reasons and reports them in its own words, so each consumer maps this onto
/// its own diagnostic with its own identity in the message.
///
/// THREE VALUES, and the two that are missing are missing deliberately. P16's
/// `MappingState` has only `Resolved` and `Unresolved`: a `FaceName` can be
/// ambiguous, which P12-STREF-001 documents, and that is exactly why this layer
/// does not map through one; `Unsupported` is absent because attribution needs
/// no surface kind at all. There is no ambiguous state to report and no
/// unsupported one.
enum class TargetProblem : std::uint8_t {
    /// The `FaceSelector` is malformed on its own terms: core's own
    /// `validate(FaceSelector)`, not a second opinion.
    SelectorInvalid,
    /// The reference names no face of the body as it is now. This is also where
    /// a face the naming chain never attributed lands, including a drilled
    /// hole's cylindrical wall. NOTHING NEARBY IS SUBSTITUTED.
    Unresolved,
    /// The reference resolved, and the mapping attributed no boundary facet to
    /// it. Reported rather than passed over: a target that covers nothing is
    /// not an empty target, it is a model whose attribution chain lost a face.
    WithoutFacets,
};

/// The three, in declaration order.
inline constexpr std::array<TargetProblem, 3> kTargetProblems{
    TargetProblem::SelectorInvalid, TargetProblem::Unresolved, TargetProblem::WithoutFacets};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(TargetProblem problem) noexcept;

/// The current boundary facets of @p face, or the reason there are none.
///
/// Ascending and without repeats, because that is what
/// `meshing::boundaryFacetsOf` returns. P16 decides which facets belong to the
/// face; nothing here measures anything.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<std::vector<meshing::ElementId>>
resolveFaceTarget(const meshing::GeometryMeshMap& map, const FaceName& face);

/// The problem `resolveFaceTarget` would report, or none if it would succeed.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<TargetProblem>
faceTargetProblem(const meshing::GeometryMeshMap& map, const FaceName& face);

} // namespace bettercad::cfd
