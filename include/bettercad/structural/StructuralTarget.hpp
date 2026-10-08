#pragma once

// P17-BC-001 -- resolving a canonical CAD face target against the current mesh.
//
// WHY THIS FILE EXISTS, AND WHY IT IS A REFACTOR RATHER THAN A NEW IDEA.
// P17-LOAD-001 needed "a FaceName, resolved through P16's mapping, or the
// reason it could not be" and wrote it in an anonymous namespace in
// `StructuralLoad.cpp`. P17-BC-001 needs exactly the same three checks for a
// restraint. Copying them would put the mapping SEMANTICS in two places, so a
// future change to P16's mapping states would have two homes and one of them
// would be missed -- which is the duplicate-rule defect P16-GEOM-001 was
// already bitten by and ADR-028 forbids in a different guise.
//
// So the resolution moved here, unchanged in behaviour, and both consumers
// call it. **That makes `StructuralLoad.cpp` a changed production file, so
// P17-LOAD-001's qualification is re-established by this milestone's
// three-preset unfiltered run** -- see `docs/verification/P17-BC-001/` and the
// dated note added to P17-LOAD-001's own evidence. Modifying a qualified
// shared path silently is exactly what the workflow forbids.
//
// WHAT IS SHARED AND WHAT IS NOT. The three checks are shared because they
// are P16's contract. The DIAGNOSTICS are not: a load "has nothing to act on"
// and a restraint "has nothing to constrain", and each carries its own
// identity. `TargetProblem` is deliberately neutral, and each consumer maps it
// onto its own problem enum with its own message.
//
// WHAT THIS FILE DOES NOT DO. It does not extract nodes -- `boundaryNodesOf`
// is P16's and already sorts and deduplicates, so a consumer calls it
// directly. And it classifies no geometry: P16-MAP-001 owns which triangles
// lie on which CAD face, and this asks rather than answers.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bettercad::structural {

/// Why a canonical CAD face target cannot be resolved against a current mesh.
///
/// NEUTRAL BY DESIGN: a load and a restraint fail for the same three reasons
/// and report them in their own words. Mapping this onto a consumer's own enum
/// is three lines and keeps the diagnostic specific.
///
/// THREE VALUES, AND THE TWO THAT ARE MISSING ARE MISSING DELIBERATELY. P16's
/// `MappingState` has only `Resolved` and `Unresolved`, and its header says
/// why: a `FaceName` "can be ambiguous, which P12-STREF-001 documents, and
/// that is exactly why this layer does not map through one", and `Unsupported`
/// is absent because "attribution needs no surface kind at all". So there is
/// no ambiguous state to report and no unsupported one.
enum class TargetProblem : std::uint8_t {
    /// The `FaceSelector` is malformed on its own terms -- core's own
    /// `validate(FaceSelector)`, not a second opinion.
    SelectorInvalid,
    /// The reference names no face of the body as it is now. This is also
    /// where a face the naming chain never attributed lands, including a
    /// drilled hole's cylindrical wall. NOTHING NEARBY IS SUBSTITUTED.
    Unresolved,
    /// The reference resolved, and the mapping attributed no boundary facet to
    /// it. Reported rather than passed over: a target that covers nothing is
    /// not an empty target, it is a model whose attribution chain lost a face.
    WithoutFacets,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(TargetProblem problem) noexcept;

/// The current boundary facets of @p face, or the reason there are none.
///
/// Ascending and without repeats, because that is what
/// `meshing::boundaryFacetsOf` returns. P16 decides which facets belong to the
/// face; nothing here measures a distance, compares a normal or tests a
/// centroid.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<std::vector<meshing::ElementId>>
resolveFaceTarget(const meshing::GeometryMeshMap& map, const FaceName& face);

/// The problem `resolveFaceTarget` would report, or none if it would succeed.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<TargetProblem>
faceTargetProblem(const meshing::GeometryMeshMap& map, const FaceName& face);

} // namespace bettercad::structural
