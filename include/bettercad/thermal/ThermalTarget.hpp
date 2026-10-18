#pragma once

// P18-LOAD-001 -- resolving a canonical CAD face target against the current
// mesh.
//
// WHY THIS FILE EXISTS. P18-LOAD-001 needs "a FaceName, resolved through P16's
// mapping, or the reason it could not be", and P18-BC-001 will need exactly the
// same three checks for a prescribed temperature and a convection surface.
// `structural::resolveFaceTarget` is the same thing for P17, but it lives in
// `structural/`, which `thermal/` may not include (ADR-050: thermal is layer 45,
// below structural at 50, so that thermo-mechanical coupling can flow down).
// So the three checks are written here, ONCE, for both thermal consumers.
//
// P17 learned the cost of doing this late: P17-LOAD-001 wrote the checks in an
// anonymous namespace, P17-BC-001 then needed them, and extracting them made
// `StructuralLoad.cpp` a changed production file that had to be requalified.
// Putting them in their own file now means P18-BC-001 includes this header and
// touches nothing qualified.
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
// NO NEAREST FACE, NO DISTANCE TO A PLANE, NO NORMAL COMPARISON, NO CENTROID
// TEST. If P16 says a name resolves to nothing, it resolves to nothing.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/Export.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bettercad::thermal {

/// Why a canonical CAD face target cannot be resolved against a current mesh.
///
/// NEUTRAL BY DESIGN: a load and a boundary condition fail for the same three
/// reasons and report them in their own words, so each consumer maps this onto
/// its own diagnostic with its own identity in the message.
///
/// THREE VALUES, and the two that are missing are missing deliberately. P16's
/// `MappingState` has only `Resolved` and `Unresolved`: a `FaceName` "can be
/// ambiguous, which P12-STREF-001 documents, and that is exactly why this layer
/// does not map through one", and `Unsupported` is absent because "attribution
/// needs no surface kind at all". There is no ambiguous state to report and no
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

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(TargetProblem problem) noexcept;

/// The current boundary facets of @p face, or the reason there are none.
///
/// Ascending and without repeats, because that is what
/// `meshing::boundaryFacetsOf` returns. P16 decides which facets belong to the
/// face; nothing here measures anything.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<std::vector<meshing::ElementId>>
resolveFaceTarget(const meshing::GeometryMeshMap& map, const FaceName& face);

/// The problem `resolveFaceTarget` would report, or none if it would succeed.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<TargetProblem>
faceTargetProblem(const meshing::GeometryMeshMap& map, const FaceName& face);

} // namespace bettercad::thermal
