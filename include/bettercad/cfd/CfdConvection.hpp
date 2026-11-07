#pragma once

// P19-CONV-001 — Convective Flux. ADR-072.
//
// `integral_V div(phi x) dV = sum_f phi_f x_f` assembled as a per-component
// scalar matrix and source, plus the one function that PRODUCES `phi`.
//
// TWO THINGS LIVE HERE, AND THE SPLIT IS DELIBERATE. `cfdMassFlux` makes a
// `phi` from a cell velocity and a density; `assembleCfdConvection` consumes a
// `phi` somebody made and assembles `div(phi .)`. The operator takes no
// density, because `phi` already carries it: `ADR-066` puts the mass variable
// at `CfdFieldLocation::Face` with dimension `mass/time`, so a kg/s face value
// is the whole of what convection needs.
//
// WHY THERE IS A PRODUCER AT ALL. `ADR-071` fixed `phi`'s sign and meaning and
// then consumed a `phi` the caller invented, and `CfdDivergence.hpp` says so:
// "P19-CONV-001 defines `phi` and its interpolation and is not authorized, so
// this operator consumes a `phi` somebody else produced." A convention with no
// producer is prose; every caller writes `rho (U . Sf)` again and re-derives
// the sign. One function makes it, and a test pins the sign.
//
// THE SIGN, ONCE, AND IT IS `ADR-071`'S UNCHANGED. A positive `phi` is flow
// from the OWNER to the neighbour across an internal face, and OUT of the
// fluid at a boundary face, matching the stored area vector (`ADR-064`). The
// assembly negates it for the neighbour and never re-derives it.
//
// THE MATRIX IS ASYMMETRIC, AND THAT IS CORRECT. Upwind writes `A(P,P) = +q`
// against `A(N,P) = -q`; central writes `A(P,N) = q(1-w)` against
// `A(N,P) = -q w`. Neither pairs into a symmetric entry unless `q` is zero, so
// this assembly passes NO `requiredSymmetryTolerance` to `fromCsr` and there is
// no constant here mirroring `kCfdDiffusionSymmetryTolerance`. Demanding
// symmetry of a convection operator would be wrong rather than merely slack
// (ADR-072 Decision 3).
//
// THE DIAGONAL IS POSITIVE, LIKE DIFFUSION'S, AND THAT IS WHY THEY ADD.
// `assembleCfdDiffusion` approximates `-div(mu grad)`; this approximates
// `+div(phi .)`. In the momentum equation both sit on the left, so
// `A_conv + A_diff` is the operator and no sign is flipped at the meeting
// point. Stated here because `P19-MOM-001` is the milestone that needs it and
// is not authorized.
//
// WHAT THIS DOES NOT DO. There is no solve: `CfdSparseMatrix` is a container
// and admitting a solve kernel is `P19-LINSOLVE-001`'s decision, so nothing
// here produces a transported field. There is no momentum assembly, no
// transient term and no time scheme. There is no limiter, no blending and no
// Peclet-based switching between the two schemes. No Peclet number is
// reported, because it needs a viscosity this operator does not take. No
// quality threshold is applied and a non-conservative `phi` is NOT refused:
// `ADR-063` finding F1 reserves a threshold to the owner and `ADR-071` refused
// the same thing for the same reason. Density is one constant, as `ADR-058`
// limits P19 v1.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

/// How an internal face's transported value is formed.
///
/// THE SCHEME APPLIES TO INTERNAL FACES ONLY. A boundary face has one cell and
/// a boundary condition, so it is treated by `ADR-072` Decision 6's rule
/// whatever this says. That is worth stating because
/// `cfdFaceInterpolationWeight` returns `1.0` for a boundary face -- correct
/// for interpolating a viscosity, and if fed to the central branch it would
/// make the face value the owner's and SILENTLY DISCARD a velocity inlet's
/// prescribed value.
enum class CfdConvectionScheme : std::uint8_t {
    /// The face value is the upwind cell's. First order, and BOUNDED given a
    /// conservative `phi` and no backflow -- see `CfdConvectionSummary`.
    Upwind,
    /// The face value is `w x_P + (1-w) x_N` with `ADR-070`'s weight. Second
    /// order and NOT bounded: it overshoots where the cell Peclet number
    /// exceeds two. Nothing here corrects, limits or blends it.
    Central,
};

/// Both, in declaration order.
inline constexpr std::array<CfdConvectionScheme, 2> kCfdConvectionSchemes{
    CfdConvectionScheme::Upwind, CfdConvectionScheme::Central};

static_assert(kCfdConvectionSchemes.size() == 2,
              "a scheme was added: update kCfdConvectionSchemes and toString, and say in "
              "ADR-072 Decision 4 why a third scheme is in scope when the checklist names two");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdConvectionScheme scheme) noexcept;

/// Why a convection system cannot be assembled, or a mass flux produced.
///
/// Declared in PHASE ORDER, and the order is asserted between phases: an input
/// with faults in two phases reports the earlier one.
///
/// THE TWO ENTRY POINTS REACH DIFFERENT SUBSETS, and each enumerator says
/// which -- the device `CfdDivergence.hpp` used for the same reason. The
/// producer reads a cell velocity and a density; the assembly reads a face
/// array and no density at all.
///
/// WITHIN A PHASE THAT WALKS THE BOUNDARY, THE FIRST OFFENDING FACET DECIDES
/// and not the enum order, which is what `CfdBoundary.hpp`, `CfdGradient.hpp`
/// and `CfdDivergence.hpp` all say of their own facet refusals.
enum class CfdConvectionProblem : std::uint8_t {
    /// The metrics describe a different mesh generation from the topology.
    /// Checked first because every later phase reads both. BOTH PATHS.
    GeometryDescribesAnotherTopology,
    /// The prepared boundary was built against a different generation.
    /// BOTH PATHS.
    ///
    /// WHAT IS ACTUALLY COMPARED, because an earlier draft of this comment
    /// promised more than the code did. It said "its stamp, its cell count or
    /// its boundary-face count disagrees", which described
    /// `assembleCfdDiffusion` and not this function: two independent reviews
    /// found it. The checks are the boundary-face count, and -- when the
    /// boundary carries a source at all -- its mesh stamp and its cell and
    /// boundary-face counts, which is `P19-DIFF-001`'s CONDITIONAL form. That
    /// milestone's correction was to make the stamp comparison conditional, not
    /// to drop it: `bindCfdBoundary` returns a default-constructed source, so
    /// comparing unconditionally would refuse every seam-built boundary. The
    /// first draft of this operator dropped it entirely and was therefore
    /// strictly weaker than the operator it cited as precedent, and a boundary
    /// prepared against a different mesh with the same boundary-face count was
    /// silently accepted.
    BoundaryDescribesAnotherTopology,
    /// A face names an owner or a neighbour the topology does not hold.
    /// BOTH PATHS.
    ///
    /// CHECKED BEFORE ANY CELL IS INDEXED, and that ordering is the whole
    /// point. `CellIndex{}` is `kInvalid`, which is `static_cast<size_t>(-1)`,
    /// and `CfdTopology::owner` returns exactly that for a face it does not
    /// hold -- so an unguarded `owner.value()` is `SIZE_MAX`. In this operator
    /// that index reaches the symbolic pass's `pattern[owner].push_back(...)`
    /// and the source's `source[owner] -= ...`, which are WRITES, and the
    /// producer's `velocityAt(cell)`, which reads three unchecked slots of the
    /// caller's span. `geometry.describes` compares the mesh stamp and the
    /// counts, never the owner and neighbour ranges, so nothing upstream
    /// establishes this.
    ///
    /// `P19-DIV-001` carries the same guard and its comment records that an
    /// independent review forced it there, after finding the guard sitting
    /// AFTER the read it exists to prevent. The first draft of this operator
    /// had no guard at all, which is that correction regressed; a review found
    /// it again. UNREACHABLE while `buildCfdTopology` holds, and recorded as
    /// such rather than omitted -- which is the argument that failed last time.
    TopologyNamesAnUnknownCell,
    /// The transported component is not one of `kVectorComponents`.
    /// ASSEMBLY ONLY.
    ///
    /// `VectorComponent` is an enum class with a fixed underlying type, so
    /// `static_cast<VectorComponent>(7)` is a well-formed value and
    /// `prescribed[7]` on a three-element array is undefined behaviour. The
    /// gate claims to be total over its inputs, so it answers for this one
    /// rather than trusting a cast that cannot be prevented.
    ComponentOutOfRange,
    /// The density is not finite, or is not strictly positive. PRODUCER ONLY.
    ///
    /// No `abs`, no `max(rho, eps)`. A negative density would reverse every
    /// flux in the mesh with no other symptom, which is `P18`'s recorded reason
    /// for refusing a negative conductivity rather than repairing it, and
    /// `ADR-069`'s for a negative viscosity.
    DensityNotUsable,
    /// The values are not the length the entry point requires: one per FACE for
    /// `phi`, which is a `Face`-located variable in `ADR-066`'s table, or three
    /// per CELL in that table's component-blocked layout for a velocity.
    ///
    /// ONE ENUMERATOR FOR TWO SHAPES, because the fault is the same fault and
    /// the message carries which was expected -- `CfdDivergence.hpp`'s choice,
    /// for its own two paths. BOTH PATHS.
    ValueLengthWrong,
    /// A supplied value is NaN or infinite. Checked AFTER the length, so a wrong
    /// length is reported as a length and not as whatever happens to sit in it.
    /// BOTH PATHS.
    ValueNotFinite,
    /// A boundary face gives the velocity the `FixedValue` policy and the
    /// prepared boundary holds no velocity for it.
    ///
    /// AN INCONSISTENCY IN THAT VALUE, not a zero: defaulting to zero would
    /// invent a wall condition nobody asked for, which `P19-DIFF-001`,
    /// `P19-GRAD-001` and `P19-DIV-001` all refused for the same reason.
    ///
    /// UNREACHABLE FROM ANY CONSTRUCTIBLE INPUT, and kept for the reason the
    /// other three record of their own copies: `PreparedCfdBoundary` has no
    /// public constructor and its one assembler sets the velocity whenever the
    /// policy is `FixedValue` or refuses by name. The state is excluded by
    /// `ADR-067`'s table and by an assembler in another module, both of which
    /// this operator only reads, so the refusal is a defence against that table
    /// moving underneath it. Recorded as UNREACHED rather than claimed tested.
    /// BOTH PATHS.
    PrescribedValueMissing,
    /// A boundary face gives the VELOCITY the `Computed` policy, which is an
    /// outcome of a solve and not a boundary condition. BOTH PATHS.
    ///
    /// NEEDED TO MAKE THE SWITCH TOTAL. `CfdBoundaryPolicy` has four values and
    /// the `U` column of `ADR-067`'s table uses three; a total switch must
    /// still answer for the fourth, and the only correct answer is a refusal.
    ///
    /// UNREACHABLE through the document path, for the reason
    /// `CfdDiffusion.hpp` gives: read down the `U` column and the only values
    /// are `FixedValue`, `ZeroGradient` and `Symmetry`.
    ///
    /// ONE ENUMERATOR FOR BOTH PATHS BECAUSE IT IS ONE REFUSAL. The transported
    /// variable is a velocity component, so its policy IS the `U` column -- the
    /// same column the producer reads for the face velocity. There is no second
    /// variable to distinguish.
    ///
    /// AND `phi`'S OWN `Computed` IS NOT REFUSED, ON EITHER PATH, WHICH IS THE
    /// OPPOSITE OF WHAT A DEFENSIVE READING SUGGESTS. `phi` is `Computed` at
    /// both pressure boundaries, because at a static-pressure inlet "the flow
    /// rate is an OUTCOME of the solve, not an input" (`ADR-058`) -- and by the
    /// time anyone assembles convection the solve has produced it. Neither path
    /// consults the `phi` column at all: the assembly takes `phi` as a face
    /// array, and the producer WRITES that column rather than reading it. A
    /// policy says where a value comes from, not whether a sum may be taken
    /// over it (`ADR-071` Decision 5, relied on here unchanged).
    ///
    /// THERE IS NO COMPILE-FAIL CASE FOR THIS ASYMMETRY, AND AN EARLIER DRAFT
    /// OF THIS COMMENT CLAIMED ONE. A review found it. The asymmetry is a
    /// RUNTIME branch -- this phase refuses the velocity's `Computed` while
    /// nothing anywhere consults the `phi` column -- and there is no type-level
    /// expression of "this policy value is accepted here and refused there", so
    /// no compile-fail case could hold it. The half that is testable is tested
    /// at runtime by
    /// `CfdConvection_AcceptsAComputedMassFluxPolicyAtBothPressureBoundaries`;
    /// the other half is the ABSENCE of a check, which a test cannot observe
    /// and which is recorded here instead.
    PolicyIsAnOutcome,
    /// `cfdFaceInterpolationWeight` has no weight for a face that must be
    /// interpolated: its orthogonal distance is not positive, or it has no unit
    /// normal.
    ///
    /// REFUSED, NOT DEFAULTED, and that is `P19-DIV-001`'s review finding
    /// applied in advance. Returning `0.5` -- or the `1.0` the weight gives a
    /// boundary face -- would produce a SUCCESSFUL system interpolated at a
    /// point nobody asked for.
    ///
    /// REACHED BY THE PRODUCER AT ANY INTERNAL FACE and by the assembly only
    /// under `Central`; `Upwind` interpolates nothing. UNREACHABLE from a
    /// constructible input, because a geometry that holds the face answers
    /// positive and a geometry that does not is phase 1. Recorded as UNREACHED.
    FaceWeightUnavailable,
    /// A produced flux, an assembled coefficient, a source entry or a reported
    /// flow total is NaN or infinite. BOTH PATHS.
    ///
    /// THIS TAG MEANS NON-FINITENESS AND NOTHING ELSE, which an earlier draft
    /// did not honour: it also carried a symbolic-pass slot miss and every
    /// `CfdSparseMatrix::fromCsr` refusal, so a reader was told a coefficient
    /// was NaN when the fault was a layout one. A review found it, and those
    /// faults now carry `AssemblyIsInconsistent`. A tag that can mean four
    /// things is the "a message is not a tag" rule inverted -- the message
    /// becomes the only correct part.
    ///
    /// THE ONLY PHASE THAT JUDGES THE ANSWER, and it exists because
    /// `P19-GRAD-001`'s adversarial review found its absence: requiring every
    /// INPUT to be finite admits `1.8e308`, from which a product or a sum can
    /// overflow to infinities of each sign, cancel to a NaN, and reach a caller
    /// with no error at all.
    ///
    /// AND IT IS OBSERVABLE AS A TAG, WHICH IS WHY THIS OPERATOR RETURNS
    /// `std::expected` RATHER THAN `Result`. `P19-DIV-001` recorded its own
    /// equivalent as limitation L10: `DivergenceIsNotFinite` judges an output,
    /// so no predictor can reach it, and because its entry points return a
    /// plain `Error` the enumerator was observable only through a message's
    /// wording -- against `CfdBoundary.hpp`'s own "a message is not a tag"
    /// rule. `CfdSparseMatrix::fromCsr` already returns a tagged failure in
    /// this module for exactly this reason, so the precedent was here. This
    /// milestone chooses its own return type rather than inheriting a recorded
    /// limitation (`ADR-072` Decision 10).
    ResultIsNotFinite,
    /// The assembly contradicted itself: the symbolic pass did not reserve a
    /// pair the numeric pass asked for, an internal face had no neighbour, or
    /// `CfdSparseMatrix::fromCsr` refused the arrays this function built.
    ///
    /// UNREACHABLE THROUGH EVERY PUBLIC PATH, and kept because the alternative
    /// is a wrong tag. The symbolic pass reserves every pair the numeric pass
    /// can ask for; the columns it builds are sorted, deduplicated and in
    /// range; and the finiteness scan runs before `fromCsr`, which is given no
    /// symmetry tolerance -- so none of the three can fire. `CfdDiffusion.hpp`
    /// records the same situation and resolves it differently, by returning an
    /// UNTAGGED `Internal` error, because `Result<T>` has no channel for a tag.
    /// This operator's channel does have one (`ADR-072` Decision 10), so the
    /// honest thing is a tag that says what happened rather than borrowing
    /// `ResultIsNotFinite`, which is what an earlier draft did.
    ///
    /// THE MESSAGE NAMES WHICH OF THE THREE, and for `fromCsr` it names the
    /// `CfdSparseProblem` as well, so the diagnostic is not weaker than the
    /// untagged form it replaces.
    AssemblyIsInconsistent,
};

/// Every problem, in phase order. `AssemblyIsInconsistent` is last because it
/// judges the assembly's own output, after every input phase.
inline constexpr std::array<CfdConvectionProblem, 12> kCfdConvectionProblems{
    CfdConvectionProblem::GeometryDescribesAnotherTopology,
    CfdConvectionProblem::BoundaryDescribesAnotherTopology,
    CfdConvectionProblem::TopologyNamesAnUnknownCell,
    CfdConvectionProblem::ComponentOutOfRange,
    CfdConvectionProblem::DensityNotUsable,
    CfdConvectionProblem::ValueLengthWrong,
    CfdConvectionProblem::ValueNotFinite,
    CfdConvectionProblem::PrescribedValueMissing,
    CfdConvectionProblem::PolicyIsAnOutcome,
    CfdConvectionProblem::FaceWeightUnavailable,
    CfdConvectionProblem::ResultIsNotFinite,
    CfdConvectionProblem::AssemblyIsInconsistent,
};

static_assert(kCfdConvectionProblems.size() == 12,
              "a problem was added or removed: update kCfdConvectionProblems, toString, and "
              "CfdConvection_TheDeclaredPhaseOrderIsTheCheckOrder, which asserts the order "
              "between the phases a constructible input can reach");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdConvectionProblem problem) noexcept;

/// A refusal with its tag.
///
/// THE TAG IS THE POINT. `P19-BC-001`'s review found eight refusals whose tags
/// could be swapped with every message assertion still passing, so a tagged
/// failure exists wherever a message would otherwise be the only evidence.
struct CfdConvectionFailure {
    CfdConvectionProblem problem{};
    Error error{};
};

/// What one assembly did, so a consumer need not re-derive it and an evidence
/// file need not guess.
///
/// THE DIMENSIONED MEMBERS ARE TYPED, not bare doubles. `CfdDivergenceSummary`
/// kept every member a count or dimensionless and put its dimensioned answers
/// on the field, because its field is a template and one struct served two
/// instantiations. `phi` is always kg/s, so there is one instantiation here and
/// `MassFlowRate` members satisfy ARCHITECTURE.md's rule directly.
struct CfdConvectionSummary {
    std::size_t cells = 0;
    /// The faces the scatter walked, split as the topology splits them.
    std::size_t internalFaces = 0;
    std::size_t boundaryFaces = 0;

    /// Boundary faces by the sign of `phi`. The two sum to `boundaryFaces`; a
    /// face with `phi == 0` counts as outflow, because `phi >= 0` is the
    /// branch it takes and a count that disagreed with the branch would be
    /// worse than useless.
    std::size_t outflowFaces = 0;
    std::size_t inflowFaces = 0;

    /// Inflow faces whose transported variable is `FixedValue`: the ones whose
    /// flux went to the SOURCE and is therefore ABSENT from the matrix. The
    /// count that makes `prescribedInflow` legible.
    std::size_t prescribedInflowFaces = 0;

    /// Inflow faces whose transported variable is `ZeroGradient` or
    /// `Symmetry`: the ones that put a NEGATIVE `phi` on the diagonal because
    /// the face value is the owner's.
    ///
    /// THIS IS THE SECOND WAY BOUNDEDNESS FAILS, and it is not the scheme's
    /// fault. It is backflow at a zero-gradient boundary, a real and known
    /// weakness of that condition, and it is what `ADR-067`'s table
    /// prescribes -- so the operator implements it and counts it rather than
    /// correcting it. Zero on any mesh whose inflow is all prescribed.
    std::size_t backflowFaces = 0;

    /// Boundary faces by the policy the `U` column gives them. The three sum to
    /// `boundaryFaces`, because `Computed` is refused and `ADR-067`'s table has
    /// no fourth value down that column.
    std::size_t fixedValueFaces = 0;
    std::size_t zeroGradientFaces = 0;
    std::size_t symmetryFaces = 0;

    /// Which scheme was used. Reported because the boundedness claim depends on
    /// it and a consumer holding only this value cannot otherwise tell.
    CfdConvectionScheme scheme = CfdConvectionScheme::Upwind;

    /// `Central` ONLY: the worst `|skewness|` over the internal faces whose
    /// value was interpolated. Zero under `Upwind`, which interpolates nothing.
    ///
    /// THE MEASURED BOUND ON THE INTERPOLATION'S OWN ERROR, the same quantity
    /// and the same reasoning as `CfdDivergenceSummary::worstSkewness`: the
    /// central face value is exact exactly where skewness is zero.
    double worstSkewness = 0.0;

    /// `sum` over boundary faces of `min(phi, 0)`: the total mass entering the
    /// mesh. Zero or negative.
    ///
    /// THE THREE TOTALS BELOW ARE INSIDE THE FINITENESS GATE. An earlier draft
    /// scanned only the coefficients and the source, so an assembly could
    /// succeed with `outflow == +inf` while every coefficient was finite --
    /// exactly the hole `ResultIsNotFinite` is documented to close, left open
    /// for the reported quantities. `CfdDivergence.cpp` checks its own totals;
    /// a review found that this one did not.
    MassFlowRate inflow{};
    /// `sum` over boundary faces of `max(phi, 0)`: the total mass leaving it.
    /// Zero or positive.
    MassFlowRate outflow{};

    /// `sum` over PRESCRIBED-inflow faces of `phi`: the part of the inflow that
    /// went to the source and is absent from the matrix. Zero or negative.
    ///
    /// THE TERM THAT MAKES THE ROW-SUM IDENTITY TRUE, and the reason it is
    /// reported rather than left to a caller. In real arithmetic, per cell,
    ///
    /// ```text
    /// rowSumSi(c) + prescribedInflow(c) == divergeCfdMassFlux(phi).at(c)
    /// ```
    ///
    /// -- scheme-independent, because both schemes' coefficients sum to `phi_f`
    /// at every face. An earlier draft of `ADR-072` wrote the term as ALL
    /// inflow, which is right only if every inflow face prescribes a value; a
    /// pressure inlet is the counterexample, since its `U` column is
    /// `ZeroGradient` and its flux stays in the matrix. Measured over 20,000
    /// mixed-policy meshes, the all-inflow form departs by a relative `0.9995`
    /// on 20,815 of 139,802 cells -- a structural error, not a rounding one.
    ///
    /// THIS MEMBER IS THE MESH TOTAL, NOT A PER-CELL ARRAY, deliberately. The
    /// per-cell term is what a test needs, and a test must RE-DERIVE it from
    /// `phi` and the boundary rather than read the operator's own number:
    /// `P19-GRAD-001`'s r5 defect was an oracle transcribed from production,
    /// and `P19-DIV-001` found the same shape again. A per-cell array here
    /// would be an invitation to that, and would be state that exists only for
    /// tests -- which ARCHITECTURE.md rejects.
    MassFlowRate prescribedInflow{};

    // THERE IS NO `boundednessIsGuaranteed` FLAG, AND THE REASON IS WORTH
    // STATING, because a first draft of this struct carried one.
    //
    // Boundedness needs three things: the scheme is `Upwind`, no face is
    // backflow, and `phi` is discretely conservative. THIS OPERATOR CANNOT SEE
    // THE THIRD. It is handed a `phi` and never asks what its divergence is --
    // that is `divergeCfdMassFlux`'s measurement, reported as
    // `CfdDivergenceSummary::relativeImbalance`. A flag named for a guarantee
    // it can only partly evaluate would be false advertising: it would read
    // true for a wildly non-conservative flux.
    //
    // So the two things the operator does know are reported as what they are --
    // `scheme` and `backflowFaces` -- and the third is left where it is
    // measured. This is `ADR-071` Decision 8's resolution: the mesh property is
    // reported and the claim is left to the suite. A summary member that cannot
    // fail, or that claims more than it checked, is decoration that reads as
    // evidence.

    friend bool operator==(const CfdConvectionSummary&, const CfdConvectionSummary&) = default;
};

/// One assembled component system: `A x = b`, approximating `div(phi x)`.
struct CfdConvectionSystem {
    /// `A`, approximating `+div(phi .)`, so the diagonal is positive under
    /// `Upwind` for a conservative `phi` and the operator ADDS to diffusion's.
    /// Square, `cells` by `cells`, and NOT symmetric -- see Decision 3.
    CfdSparseMatrix matrix;
    /// `b`, in SI, one entry per cell. Holds the prescribed-value contributions
    /// of inflow faces, and nothing else: convection has no volumetric source.
    std::vector<double> source;
    CfdConvectionSummary summary;
};

/// Everything the assembly needs that is not geometry.
struct CfdConvectionInput {
    /// `phi` at every face, in SI (kg/s), one per face, in `FaceIndex` order,
    /// INTERNAL FACES FIRST as `ADR-064` numbers them.
    ///
    /// SIGNED OUT OF THE OWNER. `cfdMassFlux` produces exactly this.
    std::span<const double> faceFlux{};
    /// Which scheme forms an internal face's value. Boundary faces are
    /// unaffected -- see `CfdConvectionScheme`.
    CfdConvectionScheme scheme = CfdConvectionScheme::Upwind;
    /// Which component is transported. A velocity assembly is three calls, one
    /// per component, exactly as `ADR-069` made diffusion's.
    VectorComponent component = VectorComponent::X;
};

/// What the producer needs.
struct CfdMassFluxInput {
    /// The cell velocities, in SI (m/s), COMPONENT-BLOCKED as `ADR-066` lays
    /// them out: `component * cells + cell`, three times the cell count.
    std::span<const double> currentVelocity{};
    /// The one density. `ADR-058` excludes a variable density from P19 v1, and
    /// `ADR-028` forbids this module from holding one: it comes through
    /// `features::requireIncompressibleFlowProperties` from the P15 material.
    Density density{};
};

/// Produces `phi` from a cell velocity field and a density: one value per face,
/// in `FaceIndex` order, SI kg/s, signed out of the owner.
///
/// `phi_f = rho (U_f . S_f)`, FACE-INTEGRATED, with no area factor, because
/// `S_f` is an area vector and the integral is the conserved quantity
/// (`ADR-071` Decision 1).
///
/// THE FACE VELOCITY, BY ROW OF `ADR-067`'S TABLE:
///
/// ```text
/// internal face     interpolated, w from cfdFaceInterpolationWeight
/// VelocityInlet     the prescribed U_b            phi = rho (U_b . S_b)
/// PressureInlet     the owner's U  (ZeroGradient) phi = rho (U_P . S_b)
/// PressureOutlet    the owner's U  (ZeroGradient) phi = rho (U_P . S_b)
/// NoSlipWall        --                            phi = 0.0  EXACTLY
/// MovingWall        --                            phi = 0.0  EXACTLY
/// Slip              --                            phi = 0.0  EXACTLY
/// ```
///
/// (`Slip` is the enumerator's spelling; `ADR-067`'s table writes the row
/// `SlipWall`.)
///
/// A WALL'S FLUX IS THE TABLE'S ZERO, NEVER A COMPUTED DOT PRODUCT, and that is
/// arithmetic rather than tidiness (`ADR-072` Decision 2). `ADR-067` gives all
/// three walls `FixedValue 0` down the `phi` column. A `MovingWall` prescribes
/// a TANGENTIAL velocity -- `P19-BC-001`'s validation requires tangency -- so
/// `w . S_b` is zero in exact arithmetic and is NOT bit-zero once the centroid,
/// the area vector and the velocity have each been rounded. A computed wall
/// flux leaks a tiny mass flow through a solid boundary, which breaks
/// conservation in a way no test of the wall itself notices: it surfaces as an
/// imbalance somewhere else.
///
/// `Computed` IN THE `phi` COLUMN IS WHAT THIS FUNCTION WRITES, not something
/// it reads. The two pressure rows are where the flow rate is an outcome, and
/// producing it from the interior velocity is what makes it one.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<std::vector<double>, CfdConvectionFailure>
cfdMassFlux(const CfdTopology& topology, const CfdGeometry& geometry,
            const PreparedCfdBoundary& boundary, const CfdMassFluxInput& input);

/// Assembles the convection operator for one component.
///
/// THE LOOP IS OVER FACES, ONCE EACH, which is what makes internal-face
/// conservation structural rather than checked: a face computes its coefficient
/// once and writes it to the owner's row and its negation to the neighbour's in
/// the same visit, so there is no second visit that could disagree. A loop over
/// cells and their faces would see each internal face twice and would have to
/// decide the upwind direction twice -- and if the two decisions disagreed,
/// because one compares `phi >= 0` and the other `-phi >= 0`, the face would
/// contribute from both cells at once and conservation would fail silently. A
/// scatter cannot express that error (`ADR-072` Decision 3).
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdConvectionSystem, CfdConvectionFailure>
assembleCfdConvection(const CfdTopology& topology, const CfdGeometry& geometry,
                      const PreparedCfdBoundary& boundary, const CfdConvectionInput& input);

/// The refusal `assembleCfdConvection` would give, with its tag, or none if it
/// would succeed.
///
/// RECORDED FOR WHAT IT IS WORTH: this shares one decision path with the
/// assembly, so their agreement cannot fail -- the limitation `P19-DIFF-001`
/// recorded as its L8, `P19-GRAD-001` as its L9 and `P19-DIV-001` as its own.
/// It is here because a caller may legitimately ask whether an input is usable
/// without paying for the answer.
///
/// IT CANNOT SEE `ResultIsNotFinite`, which judges an output this function
/// never computes. A predictor that reported it would have to do the whole
/// assembly, which is the work the predictor exists to avoid. That enumerator
/// is observable on the assembly's own tagged failure instead, which is why
/// the assembly returns one.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdConvectionFailure>
cfdConvectionFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary, const CfdConvectionInput& input);

/// The problem alone, allocation-free.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdConvectionProblem>
cfdConvectionProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary,
                     const CfdConvectionInput& input) noexcept;

/// The problem `cfdMassFlux` would report, or none. Same caveats.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdConvectionProblem>
cfdMassFluxProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                   const PreparedCfdBoundary& boundary, const CfdMassFluxInput& input) noexcept;

/// The relative tolerance the row-sum identity holds to.
///
/// `1e-13` AND NOT `1e-15`, AND THE REASON IS MEASURED. Three accumulations
/// meet in that identity -- `rowSumSi` sums a row in ascending column order,
/// `divergeCfdMassFlux` sums faces in `FaceIndex` order, and the prescribed
/// inflow is a third sum -- and the same addends in a different order are a
/// different double. Measured over 20,000 mixed-policy meshes before any of
/// this existed: worst relative departure `7.63e-15` for fluxes within two
/// orders of magnitude and `1.94e-14` across twelve, bit-exact on 31.25% to
/// 36.44%. The bound is two decades above the worst measurement, not tight to
/// it, because the measurement is of random meshes and not of every mesh.
///
/// IT IS NOT A TOLERANCE ON THE OPERATOR'S CORRECTNESS. The identity is exact
/// in real arithmetic; this is the floating-point slack, and loosening it would
/// hide a formula error rather than a rounding one.
inline constexpr double kCfdConvectionRowSumTolerance = 1.0e-13;

} // namespace bettercad::cfd
