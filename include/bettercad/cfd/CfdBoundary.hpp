#pragma once

// P19-BC-001 -- the CFD boundary conditions of one analysis, resolved against
// one mesh (ADR-067).
//
// THE AUTHORITY CHAIN, and it runs one way only:
//
//   CfdAnalysisDefinition      CANONICAL. Six kinds, each naming a CAD face by
//     .boundaryConditions      FaceName and holding typed physical values and
//     .referencePressure       nothing else about the mesh (P19-DATA-001).
//     .gravity                 Persisted, undoable, compared, fingerprinted.
//         |
//         v  resolved afresh against whatever mesh is current
//   PreparedCfdBoundary        DERIVED. One record per boundary face, the
//                              per-field treatment, and the prescribed values.
//                              Never persisted, never canonical, never in
//                              history. A remesh invalidates all of it.
//
// A FACET HANDLE, A PATCH INDEX AND A BOUNDARY FACE INDEX ARE NEVER BOUNDARY
// CONDITION AUTHORITY. Nothing in this file may be stored in a document.
//
// WHAT POSSESSION OF A PreparedCfdBoundary PROVES, at the moment of construction:
//
//   every condition is valid on its own terms and its identity is unique
//   every target resolved through P16 and got at least one facet
//   every boundary face of the topology carries EXACTLY ONE condition
//   no patch of the mesh is unnamed
//   every facet of a moving wall has a tangential wall velocity
//   every facet has a usable outward normal
//   the pressure level is fixed, by a boundary or by a located reference point
//   the topology describes the model's mesh and the geometry describes the topology
//
// WHAT IT DOES NOT PROVE. That the prescribed fluxes balance (P19-MASSBAL-001),
// that the mesh is good enough (P19-VALID-001), that the system is well posed,
// or that a solve converges. There is NO MATRIX and NO SOURCE VECTOR here:
// src/cfd/CMakeLists.txt records that this module links no linear algebra and
// that admitting a matrix is P19-LINSOLVE-001's decision. This milestone
// produces what an operator will read, not what it will assemble.

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdTarget.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

// ---------------------------------------------------------------------------
// The per-field treatment
// ---------------------------------------------------------------------------

/// What one field is at a boundary face.
///
/// FOUR VALUES, NOT TWO (ADR-067 Decision 2). The checklist asks for
/// zero-gradient and fixed-value policies; this boundary set needs two more,
/// and both are physics rather than taste.
enum class CfdBoundaryPolicy : std::uint8_t {
    /// The value is prescribed (Dirichlet).
    FixedValue,
    /// The normal derivative is zero (homogeneous Neumann).
    ZeroGradient,
    /// The NORMAL component is zero and the TANGENTIAL components are
    /// zero-gradient: one condition on a decomposition of the vector.
    ///
    /// ITS OWN VALUE AND NOT ONE OF THE FIRST TWO. `FixedValue` would claim the
    /// tangential velocity is prescribed, when it is an outcome; `ZeroGradient`
    /// would admit flow through the wall. An operator that collapsed this onto
    /// either would be silently wrong at every symmetry plane.
    Symmetry,
    /// An OUTCOME of the solve, not an input. ADR-058 says of a static-pressure
    /// inlet, in as many words, that "the flow rate is an OUTCOME of the solve,
    /// not an input". A placeholder number here would be a fabricated boundary
    /// value.
    Computed,
};

/// The four, in declaration order.
inline constexpr std::array<CfdBoundaryPolicy, 4> kCfdBoundaryPolicies{
    CfdBoundaryPolicy::FixedValue, CfdBoundaryPolicy::ZeroGradient, CfdBoundaryPolicy::Symmetry,
    CfdBoundaryPolicy::Computed};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdBoundaryPolicy policy) noexcept;

/// The policy of each of ADR-066's three variables at a boundary face.
///
/// ONE ROW OF ADR-067 DECISION 2'S FROZEN TABLE. No fourth variable: ADR-066's
/// `CfdVariable` is not an open set, and a boundary condition has to be
/// expressible without extending it.
struct CfdBoundaryTreatment {
    CfdBoundaryPolicy pressure = CfdBoundaryPolicy::ZeroGradient;
    CfdBoundaryPolicy velocity = CfdBoundaryPolicy::FixedValue;
    CfdBoundaryPolicy massFlux = CfdBoundaryPolicy::FixedValue;

    friend constexpr bool operator==(const CfdBoundaryTreatment&,
                                     const CfdBoundaryTreatment&) = default;
};

/// The treatment of @p kind. THE TABLE, and the only place it exists.
///
/// ```text
/// kind              p                U                       phi
/// VelocityInlet     ZeroGradient     FixedValue  -v n_hat    FixedValue
/// PressureInlet     FixedValue  p_b  ZeroGradient            Computed
/// PressureOutlet    FixedValue  p_b  ZeroGradient            Computed
/// NoSlipWall        ZeroGradient     FixedValue  0           FixedValue  0
/// MovingWall        ZeroGradient     FixedValue  w           FixedValue  0
/// SlipWall          ZeroGradient     Symmetry                FixedValue  0
/// ```
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdBoundaryTreatment
treatmentOf(CfdBoundaryConditionKind kind) noexcept;

// ---------------------------------------------------------------------------
// The refusals
// ---------------------------------------------------------------------------

/// Why a set of CFD boundary conditions cannot be prepared against a mesh.
///
/// EVERY VALUE IS REACHABLE and has a test. The ones that cannot be reached
/// from a `CfdModel` are reachable through `bindCfdBoundary`, which takes plain
/// inputs for exactly that reason -- the device ADR-063 and ADR-064 both used.
///
/// IDENTITY AND VALUES ARE ONE ENUMERATOR, DELIBERATELY. `validate(const
/// CfdBoundaryCondition&)` is P19-DATA-001's own rule and already distinguishes
/// an invalid identity from a non-finite value from a non-positive inflow in
/// its message. Splitting it here would mean re-deriving which, which is the
/// second opinion on core's rule this codebase refuses elsewhere; the message
/// is carried through unchanged with the condition's identity prefixed.
/// THE DECLARATION ORDER IS THE PHASE ORDER, and the order is itself policy:
/// the bindings first, so a topology or geometry for another mesh is never read
/// off; then the mesh's own naming; then every condition on its own terms,
/// BEFORE any target is resolved, for the reason thermal gives -- a malformed
/// value is malformed whether or not it multiplies something small; then
/// resolution; then coverage; then the per-facet values; then the datum.
///
/// WITHIN THE PER-FACET PASS the three value problems (`DegenerateFacet`,
/// `NonFiniteBoundaryValue`, `WallVelocityNotTangential`) are decided in one
/// walk over the boundary faces in `BoundaryFaceIndex` order, so among those
/// three it is the FIRST OFFENDING FACET that decides and not the enum order.
/// That is deterministic, and it is stated here rather than implied, because
/// claiming an order the code does not have would be a false claim about a
/// diagnostic.
///
/// THERE IS NO `TargetInvalid`, DELIBERATELY. `cfd::TargetProblem` has a
/// `SelectorInvalid`, and it cannot reach this gate: `validate(condition)`
/// applies core's own `validate(FaceSelector)` -- the same check
/// `boundaryFacetsOf` applies -- and it runs first, so a malformed selector is
/// a `ConditionInvalid` with P19-DATA-001's own message. An enumerator no input
/// can produce is worse than no enumerator, so the value is absent rather than
/// documented as unreachable. `faceTargetProblem` still reports
/// `SelectorInvalid` to a caller that has not validated, and its own test
/// covers that.
enum class CfdBoundaryProblem : std::uint8_t {
    /// The topology was not derived from the mesh the model holds. Stamps and
    /// counts, never counts alone.
    TopologyOfAnotherMesh,
    /// The geometry was not computed from that topology.
    GeometryOfAnotherTopology,
    /// A mapped CAD face with no `FaceName`, so no GeometryReference can reach
    /// it and no condition can cover it. `CfdMeshContract::unnamedPatches()`
    /// counts them; this is where the analysis is refused (CfdMesh.hpp:222).
    /// Asked early, because it is a fact about the mesh and costs one integer.
    UnnamedPatch,
    /// `validate(condition)` refused: an invalid identity, a non-finite value, a
    /// non-positive inflow speed or a zero wall velocity. P19-DATA-001's rule.
    ConditionInvalid,
    /// Two conditions share an identity. Refused rather than merged: one of
    /// them would be silently ignored.
    DuplicateConditionId,
    /// The reference names no face of the body as it is now.
    /// `TargetProblem::Unresolved`. NOTHING NEARBY IS SUBSTITUTED.
    TargetUnresolved,
    /// It resolved and the mesh gave it no facet.
    /// `TargetProblem::WithoutFacets`.
    TargetWithoutFacets,
    /// A target's facet list is not strictly ascending: it repeats a facet, or
    /// is out of order. P16 delivers its sets ascending and deduplicated, and a
    /// list that is not would count a facet twice -- and would be reported as
    /// two conditions claiming it, which names the wrong cause. Thermal refuses
    /// the same shape for the same reason (`EntitiesNotUnique`).
    TargetFacetsNotUnique,
    /// A resolved facet is not a boundary face of this topology. Unreachable
    /// once the two binding checks above pass, and refused rather than skipped:
    /// skipping it would drop a facet from the coverage count and make the
    /// coverage claim vacuous.
    FacetNotInTopology,
    /// A boundary face two conditions claim. Refused, never resolved by order,
    /// by kind or by precedence: no two CFD conditions superpose, so two on one
    /// facet is a contradiction and choosing between them would discard one of
    /// two things the user asked for.
    FacetClaimedTwice,
    /// A boundary face no condition claims. A flux would leave the domain
    /// through a face nothing prescribes (ADR-058).
    FacetWithoutCondition,
    /// A facet with no usable outward normal or no centroid. Never a zero
    /// contribution.
    ///
    /// THE ONE ENUMERATOR THIS MILESTONE CANNOT REACH, and it is recorded as
    /// unreachable rather than claimed to be tested. Possession of a
    /// `CfdGeometry` already proves every face has a finite area vector and a
    /// finite centroid -- `buildCfdGeometry` refuses a zero-area face of a cell
    /// whose volume computed positive -- and that builder is the plainest input
    /// this milestone can be given, so there is no way to present a geometry
    /// with an unusable normal. It is kept because the alternative at this site
    /// is skipping the facet, which would drop it from the coverage count and
    /// make the coverage claim vacuous.
    DegenerateFacet,
    /// A prescribed boundary value is not finite: the hydrostatic product
    /// `rho |g| (h - h_ref)` overflowed. Distinct from `DegenerateFacet`,
    /// because the facet is fine and the arithmetic is not -- an earlier draft
    /// reported this as a degenerate facet, which would have named the wrong
    /// thing on the one arm of the two that can actually happen.
    NonFiniteBoundaryValue,
    /// A moving wall's target is not PLANAR, so no single normal describes it
    /// and tangency cannot be decided on it.
    ///
    /// REFUSED RATHER THAN APPROXIMATED, which is ADR-058's rule for a
    /// condition this milestone cannot qualify. A facet normal on a meshed
    /// curved surface carries the tessellation angle, not just round-off: on a
    /// tet-meshed cylinder the axial component of a facet normal is of the order
    /// of the mesh angle, so a per-facet tangency test at any tolerance tight
    /// enough to catch a real normal component would refuse the one velocity
    /// that IS tangential to a cylinder -- an axial one. An adversarial review
    /// found exactly that, and the honest answer is that the check is sound only
    /// on a plane. ADR-058 authorizes a moving wall for a driven cavity's lid
    /// and a Couette plate, both planar, so nothing in v1's scope is lost.
    WallTargetNotPlanar,
    /// A moving wall's velocity is not tangential to its target: the wall itself
    /// does not move and there is no moving mesh (ADR-058), so a normal
    /// component would be flow through a wall. Decided against the target's
    /// COMMON normal, which exists because `WallTargetNotPlanar` ran first.
    WallVelocityNotTangential,
    /// The reference pressure or the gravity record is malformed on its own
    /// terms: a non-finite value or location, a zero or non-finite gravity
    /// vector, a non-finite reference height. P19-DATA-001's own
    /// `validate(CfdReferencePressure)` and `validate(CfdGravity)`, not a
    /// second opinion.
    ///
    /// CHECKED HERE BECAUSE THIS GATE READS THEM. An adversarial review found a
    /// zero gravity vector -- which `validate(CfdGravity)` refuses -- reaching
    /// the hydrostatic arithmetic and being reported as a non-finite product,
    /// which named the wrong cause.
    DatumInvalid,
    /// Nothing fixes the pressure level: no static-pressure boundary and no
    /// reference pressure. `PressureDatumProblem::Missing`, refused here
    /// because CfdAnalysisObject.hpp:100 says this is the gate that refuses it.
    PressureDatumMissing,
    /// A static-pressure boundary fixes the pressure AND a reference pressure is
    /// given: the reference would be ignored or would contradict the boundary.
    /// `PressureDatumProblem::Overdetermined`.
    ///
    /// `validate(definition)` refuses this too, and this gate does not assume it
    /// was called: it takes a plain `CfdAnalysisDefinition`, so a caller that
    /// never validated would otherwise get a prepared boundary whose
    /// `referenceCell()` is present while a boundary also fixes the level, and
    /// a consumer applying both would double-fix it.
    PressureDatumOverdetermined,
    /// Gravity is present and a boundary fixes the pressure, so a prescribed
    /// pressure contains `rho`, and no density was supplied. Refused by name
    /// rather than reported as a non-finite value, which is a different cause.
    HydrostaticDensityMissing,
    /// The reference pressure's point lies in no cell of the fluid.
    ReferencePointOutsideFluid,
};

/// The twenty, in declaration order.
inline constexpr std::array<CfdBoundaryProblem, 20> kCfdBoundaryProblems{
    CfdBoundaryProblem::TopologyOfAnotherMesh,
    CfdBoundaryProblem::GeometryOfAnotherTopology,
    CfdBoundaryProblem::UnnamedPatch,
    CfdBoundaryProblem::ConditionInvalid,
    CfdBoundaryProblem::DuplicateConditionId,
    CfdBoundaryProblem::TargetUnresolved,
    CfdBoundaryProblem::TargetWithoutFacets,
    CfdBoundaryProblem::TargetFacetsNotUnique,
    CfdBoundaryProblem::FacetNotInTopology,
    CfdBoundaryProblem::FacetClaimedTwice,
    CfdBoundaryProblem::FacetWithoutCondition,
    CfdBoundaryProblem::DegenerateFacet,
    CfdBoundaryProblem::NonFiniteBoundaryValue,
    CfdBoundaryProblem::WallTargetNotPlanar,
    CfdBoundaryProblem::WallVelocityNotTangential,
    CfdBoundaryProblem::DatumInvalid,
    CfdBoundaryProblem::PressureDatumMissing,
    CfdBoundaryProblem::PressureDatumOverdetermined,
    CfdBoundaryProblem::HydrostaticDensityMissing,
    CfdBoundaryProblem::ReferencePointOutsideFluid};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdBoundaryProblem problem) noexcept;

/// One refusal: which problem, and the diagnostic.
struct CfdBoundaryFailure {
    CfdBoundaryProblem problem = CfdBoundaryProblem::FacetWithoutCondition;
    Error error{};

    friend bool operator==(const CfdBoundaryFailure&, const CfdBoundaryFailure&) = default;
};

// ---------------------------------------------------------------------------
// What a boundary face carries
// ---------------------------------------------------------------------------

/// One boundary face's condition and its prescribed values.
///
/// THE PRESCRIBED VALUES ARE PER FACE, and that is the point of indexing this
/// by `BoundaryFaceIndex` rather than by condition. Under gravity a prescribed
/// pressure varies hydrostatically along a face that is not level
/// (CfdData.hpp:370), and a velocity inlet's `-v n_hat` varies over a curved
/// face. A per-condition value would be wrong in both cases.
struct CfdBoundaryFace {
    CfdBoundaryConditionId condition{};
    CfdBoundaryConditionKind kind = CfdBoundaryConditionKind::NoSlipWall;
    /// The prescribed static gauge pressure at this facet, hydrostatic part
    /// included. Present exactly when `treatmentOf(kind).pressure` is
    /// `FixedValue`.
    std::optional<Pressure> pressure{};
    /// The prescribed velocity at this facet. Present exactly when
    /// `treatmentOf(kind).velocity` is `FixedValue` -- so absent for a slip
    /// wall, whose velocity is `Symmetry`, and for both pressure boundaries.
    std::optional<Velocity3> velocity{};

    friend bool operator==(const CfdBoundaryFace&, const CfdBoundaryFace&) = default;
};

/// The forward and reverse facet counts of one condition (ADR-067 Decision 6).
///
/// ```text
/// expected  the facets P16's mapping resolved the condition's FaceName to
///           (the FORWARD answer, through boundaryFacetsOf)
/// assigned  the boundary faces the prepared binding gave that condition
///           (the REVERSE reading, counted off the binding itself)
/// ```
///
/// THE CARDINALITY IS PART OF THE ANSWER, not a diagnostic afterthought, and it
/// is what makes the coverage claim non-vacuous: counting only "every facet has
/// a condition" would pass a binding that gave a condition facets its own
/// target does not contain.
struct CfdBoundaryCardinality {
    CfdBoundaryConditionId condition{};
    std::size_t expected = 0;
    std::size_t assigned = 0;
    /// What P16's REVERSE linkage attributes to this condition: of the boundary
    /// faces the binding gave it, how many sit on a CAD face whose names
    /// include the condition's own `FaceName`.
    ///
    /// THE ONLY ONE OF THE THREE THAT CAN DISAGREE, and it is here because an
    /// adversarial review showed the other two cannot. `assigned` is counted off
    /// the same claim array that `expected` filled, and a repeated facet is
    /// refused, so `expected == assigned` holds identically on every success --
    /// a record that could not be wrong, which is not a check. `attributed`
    /// comes from `sourceFaceOf`, the other direction of P16's map, so a binding
    /// that gave a condition facets its own target does not contain shows up
    /// here as a smaller number.
    ///
    /// ABSENT ON A SEAM-BUILT VALUE, which has no mapping to ask.
    std::optional<std::size_t> attributed{};

    /// The forward and reverse counts agree and neither is zero.
    ///
    /// `expected == assigned` is an invariant the binding maintains rather than
    /// a test it can fail; what makes this predicate falsifiable is
    /// `attributed`, when it is present.
    [[nodiscard]] constexpr bool agrees() const noexcept {
        return expected == assigned && expected > 0 &&
               (!attributed.has_value() || *attributed == expected);
    }

    friend constexpr bool operator==(const CfdBoundaryCardinality&,
                                     const CfdBoundaryCardinality&) = default;
};

// ---------------------------------------------------------------------------
// Source binding
// ---------------------------------------------------------------------------

/// Everything a prepared boundary depends on (ADR-067 Decision 8).
///
/// A VALUE, NOT A HASH: no pointer, no address, no timestamp. Equality is
/// field-complete by `= default`, so a dependency added here without being
/// compared is not possible, and `staleReasons` can say WHICH moved.
struct CfdBoundarySource {
    /// Which solid.
    ObjectId body{};
    /// WHICH MESH AND WHICH GENERATION. A remesh of the same body with the same
    /// counts has a different stamp, and that alone stales a prepared boundary.
    meshing::MeshStamp mesh{};
    /// A stamp identifies the BUILDER and not the snapshot -- the lesson
    /// `MeshDofMap::describes` learned and `ThermalBoundary` records -- so the
    /// counts are compared as well.
    std::size_t cells = 0;
    std::size_t boundaryFaces = 0;
    /// The geometry revision the mapping resolved against.
    meshing::GeometryRevision geometry{};
    /// The canonical conditions it was prepared FROM, by value and in order.
    std::vector<CfdBoundaryCondition> conditions{};
    /// Both enter the prescribed values directly, so both are dependencies.
    std::optional<CfdReferencePressure> referencePressure{};
    std::optional<CfdGravity> gravity{};
    /// PRESENT EXACTLY WHEN IT ENTERED A VALUE: gravity present and some
    /// condition fixing the pressure, which is the only way `rho` reaches a
    /// prescribed value (`p_b - rho |g| (h - h_ref)`). Carrying it
    /// unconditionally would stale a gravity-free analysis on a material edit
    /// that provably cannot change anything here.
    std::optional<Density> density{};

    friend bool operator==(const CfdBoundarySource&, const CfdBoundarySource&) = default;
};

/// Which dependency moved, in the enum's order.
enum class CfdBoundaryStaleReason : std::uint8_t {
    Body,
    Mesh,
    Geometry,
    Conditions,
    Datum,
    Fluid,
};

/// The six, in declaration order.
inline constexpr std::array<CfdBoundaryStaleReason, 6> kCfdBoundaryStaleReasons{
    CfdBoundaryStaleReason::Body,       CfdBoundaryStaleReason::Mesh,
    CfdBoundaryStaleReason::Geometry,   CfdBoundaryStaleReason::Conditions,
    CfdBoundaryStaleReason::Datum,      CfdBoundaryStaleReason::Fluid};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view
toString(CfdBoundaryStaleReason reason) noexcept;

// ---------------------------------------------------------------------------
// The prepared boundary
// ---------------------------------------------------------------------------

/// One analysis's boundary conditions, resolved against one mesh.
///
/// POSSESSION IS THE EVIDENCE of the list at the top of this file. There is no
/// public constructor and one friend, so a consumer cannot be handed a partly
/// prepared boundary, and a caller that wants to skip the checks cannot
/// construct its argument. The device is `CfdModel`'s, `CfdMeshContract`'s,
/// `CfdTopology`'s, `CfdGeometry`'s and `CfdFieldMap`'s, chosen a sixth time.
///
/// ATOMIC. One refusal refuses the whole set: a partially prepared boundary is
/// not a smaller model, it is a model that claims to be the user's and is not.
class BETTERCAD_CFD_EXPORT PreparedCfdBoundary {
public:
    [[nodiscard]] std::size_t boundaryFaces() const noexcept { return faces_.size(); }

    /// The condition and prescribed values at @p boundaryFace. An empty
    /// optional for an index this boundary does not hold -- total, never
    /// undefined behaviour.
    [[nodiscard]] std::optional<CfdBoundaryFace> face(BoundaryFaceIndex boundaryFace) const noexcept;

    /// The per-field treatment at @p boundaryFace: `treatmentOf` of its kind,
    /// asked here so a consumer holding only this value needs no second lookup.
    [[nodiscard]] std::optional<CfdBoundaryTreatment>
    treatment(BoundaryFaceIndex boundaryFace) const noexcept;

    /// One record per condition, in the definition's canonical order.
    [[nodiscard]] std::span<const CfdBoundaryCardinality> cardinality() const noexcept {
        return cardinality_;
    }

    /// The cell the reference pressure's point was located in. Present exactly
    /// when the definition carried a reference pressure -- which is exactly
    /// when no boundary condition fixes the pressure (ADR-058).
    [[nodiscard]] std::optional<CellIndex> referenceCell() const noexcept { return referenceCell_; }

    [[nodiscard]] const CfdBoundarySource& source() const noexcept { return source_; }

    friend bool operator==(const PreparedCfdBoundary&, const PreparedCfdBoundary&) = default;

private:
    PreparedCfdBoundary() = default;

    /// The one assembler. Possession is the evidence only because this list and
    /// the one below are all there is.
    friend struct CfdBoundaryAssembler;
    friend BETTERCAD_CFD_EXPORT Result<PreparedCfdBoundary>
    prepareCfdBoundary(const CfdModel& model, const CfdTopology& topology,
                       const CfdGeometry& geometry, const CfdAnalysisDefinition& definition);

    CfdBoundarySource source_{};
    /// Per boundary face, in `BoundaryFaceIndex` order.
    std::vector<CfdBoundaryFace> faces_{};
    /// Per condition, in the definition's order.
    std::vector<CfdBoundaryCardinality> cardinality_{};
    std::optional<CellIndex> referenceCell_{};
};

/// Every dependency of @p boundary that differs from @p source, in the enum's
/// order. Empty if the prepared boundary still describes it.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::vector<CfdBoundaryStaleReason>
staleReasons(const PreparedCfdBoundary& boundary, const CfdBoundarySource& source);

/// Whether @p boundary still describes @p source.
///
/// A consumer calls this BEFORE reading a prescribed value. Currentness is
/// never inferred from the size of anything.
[[nodiscard]] BETTERCAD_CFD_EXPORT bool isCurrent(const PreparedCfdBoundary& boundary,
                                                  const CfdBoundarySource& source);

/// The source a prepared boundary would have for @p model, @p topology and
/// @p definition, so a consumer can compare without preparing again.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdBoundarySource
currentBoundarySource(const CfdModel& model, const CfdTopology& topology,
                      const CfdAnalysisDefinition& definition);

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

/// One condition and the facets P16 resolved its target to.
///
/// THE SEAM'S INPUT, so that a test can present a facet the topology does not
/// hold, a patch left uncovered or two conditions on one facet without needing
/// a document that produces them. `prepareCfdBoundary` is the same code driven
/// by P16's resolution.
struct CfdResolvedTarget {
    CfdBoundaryCondition condition;
    /// Ascending and without repeats, as `boundaryFacetsOf` returns them.
    std::vector<meshing::ElementId> facets{};
};

/// Binds @p targets onto @p topology, with @p geometry for the normals and the
/// centroids.
///
/// A PLAIN TOPOLOGY, A PLAIN GEOMETRY AND PLAIN TARGETS, for the reason ADR-063
/// and ADR-064 took plain inputs: a builder reachable only through the staged
/// gate could not be shown the configurations its own refusals describe.
///
/// @p unnamedPatches is the count the mesh contract carries; a non-zero count
/// refuses by `UnnamedPatch`, because coverage is then impossible.
///
/// @p mesh is the mesh the topology describes, needed to locate a reference
/// pressure's point. It is a required argument rather than an optional one
/// because an earlier draft let it be absent and then reported
/// `ReferencePointOutsideFluid` for a point it had never looked for -- the wrong
/// cause, found by review.
///
/// THE RESULT CARRIES A DEFAULT `source()`, because the seam is given no model
/// and so no body, stamp or geometry revision to record. That is the
/// CONSERVATIVE direction and not an oversight: `isCurrent` against any real
/// source is false, so a seam-built value can never claim to describe a
/// document. Only `prepareCfdBoundary` produces a value whose source means
/// anything.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<PreparedCfdBoundary>
bindCfdBoundary(const CfdTopology& topology, const CfdGeometry& geometry,
                const meshing::Mesh& mesh, std::span<const CfdResolvedTarget> targets,
                std::size_t unnamedPatches,
                const std::optional<CfdReferencePressure>& referencePressure,
                const std::optional<CfdGravity>& gravity, const std::optional<Density>& density);

/// The failure `bindCfdBoundary` would report, or none if it would succeed.
///
/// IT EXISTS BECAUSE A MESSAGE IS NOT A TAG. `bindCfdBoundary` returns only the
/// `Error`, so without this a test driving the seam could assert what a refusal
/// SAYS and never which problem it IS -- and a review found eight refusals whose
/// tag could therefore be swapped for another with nothing noticing. The same
/// pass, so the two cannot drift.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdBoundaryFailure>
bindCfdBoundaryFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                       const meshing::Mesh& mesh, std::span<const CfdResolvedTarget> targets,
                       std::size_t unnamedPatches,
                       const std::optional<CfdReferencePressure>& referencePressure,
                       const std::optional<CfdGravity>& gravity,
                       const std::optional<Density>& density);

/// Resolves and binds the boundary conditions of @p definition against the mesh
/// @p model holds.
///
/// IT TAKES A CfdModel, which answers the stale-input question without a check
/// here: possession proves the geometry and the mesh are current (ADR-060). It
/// takes the topology and the geometry as arguments rather than building them,
/// so a caller holding either for a DIFFERENT mesh is refused rather than
/// silently rebuilt.
///
/// It reads `boundaryConditions`, `referencePressure` and `gravity` of the
/// definition and nothing else, so a mode, iteration, convergence,
/// linear-solver, output or initial-condition edit cannot change the answer.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<PreparedCfdBoundary>
prepareCfdBoundary(const CfdModel& model, const CfdTopology& topology, const CfdGeometry& geometry,
                   const CfdAnalysisDefinition& definition);

/// The failure `prepareCfdBoundary` would report, or none if it would succeed.
/// The same checks in the same order, behind the same pass.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdBoundaryFailure>
cfdBoundaryFailure(const CfdModel& model, const CfdTopology& topology, const CfdGeometry& geometry,
                   const CfdAnalysisDefinition& definition);

/// The problem alone, for a caller that wants to present or count the reason.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdBoundaryProblem>
cfdBoundaryProblem(const CfdModel& model, const CfdTopology& topology, const CfdGeometry& geometry,
                   const CfdAnalysisDefinition& definition);

// ---------------------------------------------------------------------------
// The pressure datum
// ---------------------------------------------------------------------------

/// The cell of @p topology containing @p point, or none if no cell does.
///
/// THE LOWEST `CellIndex` THAT CONTAINS IT, so a point on a shared face, edge
/// or vertex resolves to one cell and the same cell in every build. The test is
/// on the four barycentric sub-volumes of the tetrahedron, each against a
/// tolerance PROPORTIONAL TO THE CELL'S OWN VOLUME (`1e-9 * |V|`) rather than
/// an absolute one: a cell's size is set by the mesh, so an absolute epsilon
/// would be generous in a coarse mesh and meaningless in a fine one.
///
/// A LINEAR SCAN, and no acceleration structure. The question is asked once per
/// gate, so there is nothing to build, keep current or invalidate (ADR-067
/// Decision 7).
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CellIndex>
locatePoint(const CfdTopology& topology, const meshing::Mesh& mesh, const Point3D& point);

/// The height of @p point along `-g` from the model origin, in metres.
///
/// `h = -(x . g_hat)`, so it increases upward whatever direction gravity is.
/// This is the `h(x)` of CfdData.hpp's hydrostatic rule.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<Length>
heightAlongGravity(const Point3D& point, const Acceleration3& gravity);

/// The static gauge pressure prescribed at a facet whose centroid is
/// @p centroid, for a boundary value @p prescribed under @p gravity in a fluid
/// of density @p density.
///
/// `p_f = p_b - rho |g| (h(centroid) - referenceHeight)` (ADR-058, CfdData.hpp:370).
/// Without gravity there is no height and the answer is @p prescribed itself.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<Pressure>
hydrostaticPressure(Pressure prescribed, const Point3D& centroid,
                    const std::optional<CfdGravity>& gravity,
                    const std::optional<Density>& density);

} // namespace bettercad::cfd
