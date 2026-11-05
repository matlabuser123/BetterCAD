// Build-failure tests for the CFD boundary conditions (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line
// and not to a missing header.
//
// WHAT THESE PROVE is P19-BC-001's structural half (ADR-067), and it is a
// different claim from the field layout's. The field group proved that
// SUBSCRIPTS do not mix; this one proves that a boundary condition's AUTHORITY
// cannot be faked.
//
// FIRST, THAT A PREPARED BOUNDARY CANNOT BE CONJURED. Possession of one is the
// evidence that every condition resolved, every facet carries exactly one
// condition, no patch is unnamed and the datum is fixed (ADR-067 Decision 1).
// That claim is worth nothing if a consumer can default-construct one, so the
// constructor is private with one friend and these cases are what holds it.
//
// SECOND, THAT CANONICAL INTENT HOLDS NOTHING MESH-LOCAL. The checklist's "no
// mesh face IDs in canonical BC intent" is a structural claim, not a review
// note: a `CfdBoundaryCondition` has no facet handle, no `BoundaryFaceIndex`
// and no patch, and a `CfdAnalysisDefinition` has no member for a resolved set.
// A remesh must leave every condition's payload valid (ADR-032), which is only
// true if there is nothing in it to invalidate.
//
// THIRD, THAT THE POLICY IS A POLICY AND NOT A BOOLEAN. ADR-067 Decision 2
// needs four values precisely because two would collapse `Symmetry` onto
// something wrong. A `CfdBoundaryPolicy` that converted to `bool` or to an
// integer would let an operator write `if (treatment.velocity)` and get a
// plausible answer at a symmetry plane, which is the defect the fourth value
// exists to prevent.
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdTarget.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstddef>
#include <optional>
#include <span>

using namespace bettercad;
using namespace bettercad::cfd;

namespace {

// EVERY ONE IS [[maybe_unused]], AND THAT IS NOT TIDYING. Each is called only
// from inside a `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL
// configuration -- no macro, the build every preset runs -- they are unused, and
// this translation unit is compiled with -Werror.

#if defined(BETTERCAD_CF_FABRICATE_A_PREPARED_BOUNDARY)
// Possession is the evidence only because this is impossible.
[[maybe_unused]] PreparedCfdBoundary fabricate() {
    return PreparedCfdBoundary{};
}
#endif

#if defined(BETTERCAD_CF_A_CONDITION_HOLDS_A_FACET)
// A canonical condition names a CAD face and nothing about a mesh.
[[maybe_unused]] meshing::ElementId facetOfCondition(const CfdBoundaryCondition& condition) {
    return condition.facet();
}
#endif

#if defined(BETTERCAD_CF_A_CONDITION_HOLDS_A_BOUNDARY_FACE)
[[maybe_unused]] BoundaryFaceIndex slotOfCondition(const CfdBoundaryCondition& condition) {
    return condition.boundaryFace();
}
#endif

#if defined(BETTERCAD_CF_AN_INLET_HOLDS_A_PATCH)
// The payloads carry typed physical values and a FaceName. Not a patch.
[[maybe_unused]] PatchIndex patchOfInlet() {
    VelocityInlet inlet;
    return inlet.patch;
}
#endif

#if defined(BETTERCAD_CF_A_DEFINITION_HOLDS_A_PREPARED_BOUNDARY)
// ADR-067 Decision 1: a prepared boundary is never a member of anything
// canonical, so a remesh cannot orphan it inside a document.
[[maybe_unused]] std::size_t preparedInDefinition(const CfdAnalysisDefinition& definition) {
    return definition.preparedBoundary.boundaryFaces();
}
#endif

#if defined(BETTERCAD_CF_A_DEFINITION_HOLDS_RESOLVED_FACETS)
[[maybe_unused]] std::size_t resolvedInDefinition(const CfdAnalysisDefinition& definition) {
    return definition.resolvedFacets.size();
}
#endif

#if defined(BETTERCAD_CF_A_POLICY_IS_A_BOOLEAN)
// If this compiled, `if (treatment.velocity)` would be true for Symmetry.
[[maybe_unused]] bool policyAsBool(const CfdBoundaryTreatment& treatment) {
    bool fixed = treatment.velocity;
    return fixed;
}
#endif

#if defined(BETTERCAD_CF_A_POLICY_IS_AN_INTEGER)
[[maybe_unused]] int policyAsInt(const CfdBoundaryTreatment& treatment) {
    int which = treatment.pressure;
    return which;
}
#endif

#if defined(BETTERCAD_CF_AN_INTEGER_IS_A_POLICY)
[[maybe_unused]] CfdBoundaryTreatment treatmentFromInts() {
    CfdBoundaryTreatment treatment;
    treatment.massFlux = 1;
    return treatment;
}
#endif

#if defined(BETTERCAD_CF_A_TARGET_PROBLEM_IS_A_BOUNDARY_PROBLEM)
// The neutral three and the consumer's sixteen are different alphabets: each
// consumer maps one onto the other in its own words.
[[maybe_unused]] CfdBoundaryProblem targetAsBoundary(TargetProblem problem) {
    CfdBoundaryProblem mapped = problem;
    return mapped;
}
#endif

#if defined(BETTERCAD_CF_A_PRESCRIBED_PRESSURE_IS_A_VELOCITY)
// Units are part of correctness: a prescribed pressure cannot be read where a
// velocity belongs, even though both are one double underneath.
[[maybe_unused]] Velocity pressureAsVelocity(const CfdBoundaryFace& face) {
    Velocity speed = *face.pressure;
    return speed;
}
#endif

#if defined(BETTERCAD_CF_A_RAW_INDEX_SUBSCRIPTS_A_BOUNDARY)
// A boundary face is addressed by `BoundaryFaceIndex`, never by a bare integer.
[[maybe_unused]] std::optional<CfdBoundaryFace> rawSubscript(const PreparedCfdBoundary& boundary) {
    return boundary.face(0);
}
#endif

#if defined(BETTERCAD_CF_A_MESH_HANDLE_SUBSCRIPTS_A_BOUNDARY)
[[maybe_unused]] std::optional<CfdBoundaryFace> handleSubscript(
    const PreparedCfdBoundary& boundary, meshing::ElementId facet) {
    return boundary.face(facet);
}
#endif

#if defined(BETTERCAD_CF_THE_CARDINALITY_IS_WRITABLE)
// The records are a read-only view: a consumer cannot adjust a count to make it
// agree.
[[maybe_unused]] void rewriteCardinality(const PreparedCfdBoundary& boundary) {
    boundary.cardinality()[0].assigned = 0;
}
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_IS_WRITABLE)
// The source binding is what `staleReasons` compares; editing it through the
// prepared value would let a stale boundary call itself current.
[[maybe_unused]] void rewriteSource(const PreparedCfdBoundary& boundary) {
    boundary.source().cells = 0;
}
#endif

#if defined(BETTERCAD_CF_A_BOUNDARY_FACE_INDEX_IS_A_CELL_INDEX)
// The datum's answer is a CellIndex and a boundary record's subscript is a
// BoundaryFaceIndex; neither converts to the other.
[[maybe_unused]] CellIndex referenceAsBoundaryFace(const PreparedCfdBoundary& boundary) {
    CellIndex cell = BoundaryFaceIndex::fromValue(0);
    return boundary.referenceCell().value_or(cell);
}
#endif

} // namespace
