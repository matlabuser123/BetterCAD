#pragma once

// P19-MOM-001 — Momentum Equation. ADR-073.
//
// The incompressible momentum equation for ONE velocity component, assembled
// into a cell-by-cell sparse system:
//
// ```text
// rho (dU/dt + div(U U)) = -grad p + mu lap U + rho g
// ```
//
// integrated over a finite volume and arranged with every implicit term on the
// left:
//
// ```text
//   rho V / dt            * U_P          the transient term, backward Euler
// + sum_f phi_f U_f                      convection, ADR-072
// - sum_f mu (grad U)_f . S_f            diffusion, ADR-069
// =  rho V / dt           * U_P^n        the previous time level
// -  V (grad p)_P                        the pressure gradient
// +  rho V g                             the body force
// +  b_conv + b_diff                     the boundary contributions
// ```
//
// THIS OPERATOR IS THE SUM OF TWO QUALIFIED OPERATORS AND NOTHING ELSE
// (ADR-073 Decision 1). It calls `assembleCfdConvection` and
// `assembleCfdDiffusion`, verifies their compressed-sparse-row patterns are
// identical, sums them slot-wise and adds the transient diagonal. It does not
// walk a face, does not decide an upwind direction and does not form an
// over-relaxed decomposition: a second copy of a qualified discretisation is
// the "second parameter system" ARCHITECTURE.md rejects, in miniature, and its
// divergence from the first would be silent. `P19-CONV-001`'s review found one
// three-line interpolation duplicated across three milestones with the same
// defect left in it three times; a fourth copy of the upwind decision would be
// that mistake at a larger scale.
//
// AND THAT IS WHY THE TERMS STAY SEPARABLE. The checklist asks for "no hidden
// stabilization", which is a NEGATIVE claim, and a negative claim with no test
// is prose. Because the assembly IS the sum, the suite can assert that the
// matrix equals `(A_conv + A_diff)` slot-wise with the transient diagonal added
// after, BIT-EXACTLY -- so any added term, however small, fails it. That
// assertion is the whole falsifiable content of the item, and composition is
// what buys it (ADR-073 Decision 9).
//
// THE SIGNS MEET WITHOUT A FLIP, AND `CfdConvection.hpp` SAYS SO ALREADY:
// "`assembleCfdDiffusion` approximates `-div(mu grad)`; this approximates
// `+div(phi .)`. In the momentum equation both sit on the left, so
// `A_conv + A_diff` is the operator and no sign is flipped at the meeting
// point. Stated here because `P19-MOM-001` is the milestone that needs it."
// This is that milestone, and the sentence is relied on unchanged.
//
// THE ACCUMULATION ORDER IS PART OF THE CONTRACT, BECAUSE IT WAS MEASURED.
// `((conv + diff) + transient)` and `(conv + (diff + transient))` are the same
// double on 78.06% of 200,000 random triples, worst relative departure
// `4.034e-13`; the five source terms summed in the declared order and in
// reverse agree on 57.45%, worst `3.044e-13`. So NO order-independence is
// claimed anywhere here. The assembly fixes one order, states it below, and
// the suite reproduces THAT order rather than a mathematically equal one --
// which is exactly why the term decomposition can be asserted bit-exactly
// while the row-sum additivity cannot (ADR-073's Q1, Q3, Q5).
//
// WHAT THIS DOES NOT DO. There is no solve, so no velocity field is produced
// and no equation is advanced: `CfdSparseMatrix` is a container and admitting a
// solve kernel is `P19-LINSOLVE-001`'s decision. There is no continuity
// equation (`P19-CONT-001`) and no pressure-velocity coupling, so no Rhie-Chow
// interpolation (`P19-PRES-001`); both are unauthorized. There is no
// under-relaxation -- `CfdIterationSettings`' factors belong to the outer
// iteration, and applying them in the assembly would put the solver's state in
// the operator. There is no Courant number and no step is refused as too
// large: `ADR-063` finding F1 reserves an engineering threshold to the owner,
// and `ADR-071` and `ADR-072` refused the same thing for the same reason. No
// turbulence, no buoyancy, no temperature coupling: one constant density and
// one constant viscosity, as `ADR-058` limits P19 v1.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>
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
#include <type_traits>
#include <vector>

namespace bettercad::cfd {

// ---------------------------------------------------------------------------
// The dimensional identities, proved at compile time
// ---------------------------------------------------------------------------
//
// EVERY COEFFICIENT OF THE MOMENTUM MATRIX IS A MASS FLOW RATE, AND EVERY
// SOURCE ENTRY IS A FORCE. That is not a comment: it is seven
// `static_assert`s over the units framework's own `decltype` arithmetic, so a
// term whose formula lost a density or a volume does not compile.
//
// THESE BITE ONLY BECAUSE THE ASSEMBLY COMPUTES IN TYPED QUANTITIES, AND AN
// EARLIER DRAFT DID NOT. It formed every term in raw doubles via `.si()`, so
// `rho * volume * dt` would have compiled, satisfied all seven assertions,
// passed every finiteness gate, and been caught only by a runtime oracle -- the
// assertions proved the units framework's own dimension algebra and nothing
// this milestone wrote. An independent review found it, and the inversion was
// exact: the runtime test was the strongest check in the milestone and these
// were the weakest, which is the opposite of what ADR-073 Decision 3 claimed
// for them. `assembleCfdMomentum` now binds the transient coefficient to a
// `MassFlowRate` and each locally formed source term to a `Force`, so a term
// that lost a density, a volume or a time DOES NOT COMPILE. The arithmetic is
// unchanged -- `operator*` and `operator/` are `fromSi(a.si() OP b.si())`, so
// the grouping and every bit are what they were.
//
// A TERM WITH THE WRONG DIMENSIONS IS THE FAILURE MODE A NUMERICAL TEST IS
// WORST AT CATCHING: it produces finite, plausible, order-of-magnitude-wrong
// numbers that pass every finiteness gate and every symmetry gate, and on a
// fixture whose volumes are near one it can pass a magnitude assertion too.
// ARCHITECTURE.md's "units are part of correctness" is usually kept by a typed
// accessor; here it is kept by a proof (ADR-073 Decision 3).

/// `rho V / dt`: the transient diagonal.
using CfdTransientCoefficient = decltype(Density{} * Volume{} / Time{});
/// `mu A / d`: diffusion's implicit coefficient.
using CfdDiffusionCoefficient = decltype(DynamicViscosity{} * Area{} / Length{});

static_assert(std::is_same_v<CfdTransientCoefficient, MassFlowRate>,
              "the transient coefficient rho V / dt must be a mass flow rate, the same quantity "
              "as phi and as mu A / d, or the three terms of the matrix are not commensurate and "
              "their sum is not a momentum operator");
static_assert(std::is_same_v<CfdDiffusionCoefficient, MassFlowRate>,
              "diffusion's mu A / d must be a mass flow rate: Pa s m^2 / m is kg/s, and if this "
              "fails the diffusion term has lost or gained a length");

static_assert(std::is_same_v<decltype(MassFlowRate{} * Velocity{}), Force>,
              "a coefficient times a velocity must be a force, or the assembled system A U = b is "
              "not an equation of motion");
static_assert(std::is_same_v<decltype(Volume{} * PressureGradient{}), Force>,
              "the pressure term V grad p must be a force; if this fails the gradient is per-area "
              "rather than per-length, or the volume factor is missing");
static_assert(std::is_same_v<decltype(Density{} * Volume{} * Acceleration{}), Force>,
              "the body force rho V g must be a force");
static_assert(std::is_same_v<decltype(CfdTransientCoefficient{} * Velocity{}), Force>,
              "the transient source rho V / dt * U^n must be a force. STRICTLY IMPLIED, like the "
              "assertion below it, by CfdTransientCoefficient being a mass flow rate and a mass "
              "flow rate times a velocity being a force. An earlier draft labelled the NEXT "
              "assertion as a corollary and left this one unmarked, so this file and ADR-073 both "
              "said 'seven' as though all seven bit; the honest count is FIVE independent "
              "identities and TWO corollaries, and an independent review found the unmarked one");
static_assert(std::is_same_v<decltype(MassFlowRate{} * Velocity{}),
                             decltype(Volume{} * PressureGradient{})>,
              "the convective and the pressure term must be the same dimension AS EACH OTHER. "
              "STRICTLY IMPLIED by the two assertions above, each of which names Force, and it "
              "catches nothing they do not -- an area in place of the volume gives Force/Length, "
              "which the Force assertion already refuses. It is kept because the equation's own "
              "statement is that these two terms balance, and an assertion of that balance is "
              "worth one line even when it is a corollary. A first draft of this comment claimed "
              "it caught a case the others missed, which was false");

// ---------------------------------------------------------------------------
// The refusals
// ---------------------------------------------------------------------------

/// Why a momentum system cannot be assembled.
///
/// Declared in PHASE ORDER, and the order is asserted between the phases a
/// constructible input can reach: an input with faults in two phases reports
/// the earlier one.
///
/// THE LAST TWO JUDGE THE OUTPUT AND THE ASSEMBLY ITSELF; everything before
/// them reads an input.
enum class CfdMomentumProblem : std::uint8_t {
    /// The metrics describe a different mesh generation from the topology.
    /// Checked first because every later phase reads both.
    GeometryDescribesAnotherTopology,
    /// The prepared boundary was built against a different generation.
    ///
    /// THE CONDITIONAL FORM, which is `P19-DIFF-001`'s correction and
    /// `P19-CONV-001`'s inheritance of it: the boundary-face count always, and
    /// -- only when the boundary carries a source at all -- its mesh stamp and
    /// its cell and boundary-face counts. `bindCfdBoundary` returns a
    /// default-constructed source, so comparing unconditionally would refuse
    /// every seam-built boundary.
    BoundaryDescribesAnotherTopology,
    /// A face names an owner or a neighbour the topology does not hold.
    ///
    /// CHECKED HERE RATHER THAN LEFT TO THE SUB-GATES, AND THAT IS A
    /// DIAGNOSTIC DECISION. `cfdConvectionProblem` would catch it and this
    /// function would report `ConvectionRefusedTheInput` -- naming convection
    /// for a fault that is about the topology. The message names the face.
    ///
    /// THIS OPERATOR DOES NOT ITSELF INDEX BY OWNER OR NEIGHBOUR: it sums
    /// slots and walks cells, so the unguarded `SIZE_MAX` that
    /// `P19-DIV-001`'s and `P19-CONV-001`'s reviews each found reaching writes
    /// has no path here. The check is kept because the gate is declared total
    /// over its inputs and because the sub-operators do index that way.
    /// UNREACHABLE while `buildCfdTopology` holds, and recorded as such.
    TopologyNamesAnUnknownCell,
    /// The assembled component is not one of `kVectorComponents`.
    ///
    /// `VectorComponent` is an enum class with a fixed underlying type, so
    /// `static_cast<VectorComponent>(7)` is a well-formed value and indexing a
    /// three-element array with it is undefined behaviour. The gate claims to
    /// be total over its inputs, so it answers for this one.
    ComponentOutOfRange,
    /// The convection scheme is not one of `kCfdConvectionSchemes`.
    ///
    /// THE SAME ARGUMENT AS `ComponentOutOfRange`, AND AN EARLIER DRAFT MADE IT
    /// FOR ONE AND NOT THE OTHER -- an independent review found the asymmetry.
    /// `CfdConvectionScheme` is an enum class with a fixed underlying type, so
    /// `static_cast<CfdConvectionScheme>(7)` is a well-formed value.
    ///
    /// IT IS NOT UNDEFINED BEHAVIOUR, AND THAT IS WHY IT IS ITS OWN ENUMERATOR
    /// RATHER THAN A SECOND USE OF THE COMPONENT'S. `assembleCfdConvection`
    /// forms `central = (scheme == Central)`, which is false for 7, so the
    /// system would be assembled with UPWIND -- a scheme the caller did not ask
    /// for, reported back as 7 by the summary and as an unknown name by
    /// `toString`. A silent substitution is the class of fault this gate exists
    /// to refuse, and the boundedness statements below are made per scheme.
    SchemeOutOfRange,
    /// The density is not finite, or is not strictly positive.
    ///
    /// ALWAYS REQUIRED, EVEN WHERE IT IS UNUSED, and that is deliberate. It
    /// enters only the transient term and the body force -- `phi` already
    /// carries it in convection (`ADR-066` puts the mass variable at kg/s) --
    /// so a steady, gravity-free assembly reads it nowhere. Making the
    /// requirement conditional would make a fluid property's validity depend
    /// on which terms were switched on, and a caller that then enabled gravity
    /// would be refused by a value it had already supplied.
    ///
    /// No `abs`, no `max(rho, eps)`: a negative density would reverse the body
    /// force with no other symptom, which is P18's recorded reason for
    /// refusing a negative conductivity rather than repairing it.
    DensityNotUsable,
    /// The viscosity is not finite, or is not strictly positive. `ADR-069`'s
    /// own refusal, checked here so the diagnostic names the viscosity rather
    /// than naming diffusion.
    ViscosityNotUsable,
    /// A gravity record is present and its vector is not finite, or is exactly
    /// zero.
    ///
    /// A ZERO VECTOR IS REFUSED, NOT TREATED AS "NO GRAVITY", because
    /// `CfdData.hpp` is explicit that "NO GRAVITY IS THE ABSENCE OF THIS
    /// RECORD, not a zero vector: a zero vector would be a second spelling of
    /// none", and `validate(CfdGravity)` refuses one. Accepting it here would
    /// admit a state the canonical type forbids.
    ///
    /// `referenceHeight` IS NOT READ AT ALL and so cannot be refused here. It
    /// exists for prescribed boundary PRESSURES: `ADR-058` puts the
    /// hydrostatic part inside `p`, and `P19-BC-001` already varies a
    /// prescribed pressure along a face as `p_b - rho |g| (h - h_ref)`.
    /// Reading it again would apply the hydrostatic field twice.
    GravityNotUsable,
    /// A time step is present and is not finite, or is not strictly positive.
    TimeStepNotUsable,
    /// The transient inputs contradict each other: a time step with no
    /// previous velocity, or a previous velocity with no time step.
    ///
    /// ONE ENUMERATOR FOR BOTH DIRECTIONS, and the message says which.
    ///
    /// AN INERT INPUT IS REFUSED RATHER THAN IGNORED. P18 refused an initial
    /// temperature on a steady analysis for the same reason, and
    /// `CfdData.hpp` made the absence of gravity the absence of a record for
    /// the same reason: a value that does nothing is a caller's mistake, and
    /// accepting it silently is how that mistake survives to the next
    /// milestone.
    TransientInputsAreInconsistent,
    /// A supplied array is not the length its entry requires: one per FACE for
    /// `phi` and for the face gradient, one per CELL for the pressure
    /// gradient, three per cell in `ADR-066`'s component-blocked layout for
    /// either velocity.
    ///
    /// ONE ENUMERATOR FOR FIVE ARRAYS OF THREE SHAPES -- `faces`, `cells` and
    /// `3 * cells`; an earlier draft said "five shapes" and an independent
    /// review counted them -- because the fault is the same fault and
    /// the message carries which array and which length was expected --
    /// `CfdDivergence.hpp`'s and `CfdConvection.hpp`'s choice for their own
    /// paths.
    ValueLengthWrong,
    /// A supplied value is NaN or infinite. Checked AFTER every length, so a
    /// wrong length is reported as a length and not as whatever happens to sit
    /// in it.
    ValueNotFinite,
    /// `assembleCfdConvection` would refuse this input. The message names the
    /// `CfdConvectionProblem`.
    ///
    /// PREDICTED, NOT DISCOVERED (ADR-073 Decision 7). The gate calls
    /// `cfdConvectionProblem` rather than re-deriving twelve checks that would
    /// then have to agree with that operator forever. The consequence is that
    /// this gate is exactly as complete as that predictor, which shares one
    /// decision path with its own assembly -- `P19-DIFF-001`'s L8,
    /// `P19-GRAD-001`'s L9, `P19-DIV-001`'s own limitation, inherited here and
    /// not pretended away.
    ConvectionRefusedTheInput,
    /// `assembleCfdDiffusion` would refuse this input. The message names the
    /// `CfdDiffusionProblem`. Same mechanism, same inherited limitation.
    DiffusionRefusedTheInput,
    /// An assembled coefficient or a source entry is NaN or infinite.
    ///
    /// THE ONLY PHASE THAT JUDGES THE ANSWER, and it exists because
    /// `P19-GRAD-001`'s adversarial review found its absence: requiring every
    /// INPUT to be finite admits `1.8e308`, from which a product or a sum can
    /// overflow to infinities of each sign, cancel to a NaN, and reach a
    /// caller with no error at all.
    ///
    /// REACHABLE BY THREE ROUTES, AND AN EARLIER DRAFT NAMED ONLY THE WEAKEST
    /// OF THEM. It said "both of them gate their own output", which is false of
    /// diffusion's SOURCE: `assembleCfdDiffusion` never checks its right-hand
    /// side for finiteness, and `CfdSparseMatrix::fromCsr`'s `NonFiniteEntry`
    /// covers the stored values only -- so a `FixedValue` face with a
    /// prescribed velocity component of `1e308` and a finite `Gamma` yields an
    /// infinite source in a system returned SUCCESSFULLY. An independent review
    /// found it. The three routes, easiest first:
    ///
    /// ```text
    /// 1  diffusion's source is ungated upstream, and THIS is its gate
    /// 2  rho V / dt overflows from three factors each of which passed its own
    ///    phase
    /// 3  a finite convection matrix and a finite diffusion matrix sum to an
    ///    infinity
    /// ```
    ResultIsNotFinite,
    /// The assembly contradicted itself: the two sub-operators' sparse
    /// patterns differ, a sub-assembly refused an input this gate accepted, or
    /// `CfdSparseMatrix::fromCsr` refused the arrays this function built.
    ///
    /// THE PATTERN COMPARISON IS THE INTERESTING ONE (ADR-073 Decision 2).
    /// Both operators build a diagonal for every cell plus both off-diagonals
    /// of every internal face, through the same `layoutOf`, so the patterns
    /// are identical and a slot-wise sum is exact. RELYING ON THAT BY READING
    /// THE CODE WOULD BE A COUPLING WITH NO GATE, so the arrays are compared
    /// element by element and the message names the first disagreeing
    /// position.
    ///
    /// The alternative was to sum through `coeffSi`, which tolerates differing
    /// patterns -- and which would SILENTLY DROP any coefficient falling
    /// outside this function's own pattern. A refusal is better than a drop: a
    /// dropped coefficient is a conservation error with no symptom.
    ///
    /// REACHED, AND AN EARLIER DRAFT OF THIS COMMENT CLAIMED IT WAS
    /// UNREACHABLE THROUGH EVERY PUBLIC PATH. An independent review traced the
    /// route. `assembleCfdDiffusion` returns `Result<CfdDiffusionSystem>`,
    /// whose channel carries no tag, so when a viscosity of `1.5e308` --
    /// finite, strictly positive, accepted by phase 7 and by
    /// `cfdDiffusionProblem` -- overflows `mu A / d`, this operator cannot tell
    /// an output overflow from a genuine inconsistency and must not guess. The
    /// tag then says what momentum actually knows: a predictor accepted an
    /// input its own assembly refused. `CfdDiffusion.hpp` removed its own
    /// `AssembledMatrixInconsistent` for the same no-channel reason, so this is
    /// `P19-DIFF-001`'s limitation inherited rather than a new one, and it is
    /// recorded as a limitation instead of denied.
    ///
    /// THE CONVECTION SIDE IS NOT ROUTED HERE, because its channel DOES carry a
    /// tag: a convection refusal of `ResultIsNotFinite` is reported as
    /// momentum's own `ResultIsNotFinite`, which is what it is. The earlier
    /// draft mapped it here, and so reported a non-finite result as a predictor
    /// disagreeing with its own assembly when the two had agreed perfectly.
    ///
    /// THE OTHER THREE ROUTES REMAIN UNREACHABLE and are recorded as UNREACHED:
    /// the two sparse patterns cannot differ while both operators keep their
    /// present symbolic pass, a row cannot lack its diagonal, and `fromCsr`
    /// cannot refuse what this assembly builds.
    AssemblyIsInconsistent,
};

/// Every problem, in phase order.
inline constexpr std::array<CfdMomentumProblem, 16> kCfdMomentumProblems{
    CfdMomentumProblem::GeometryDescribesAnotherTopology,
    CfdMomentumProblem::BoundaryDescribesAnotherTopology,
    CfdMomentumProblem::TopologyNamesAnUnknownCell,
    CfdMomentumProblem::ComponentOutOfRange,
    CfdMomentumProblem::SchemeOutOfRange,
    CfdMomentumProblem::DensityNotUsable,
    CfdMomentumProblem::ViscosityNotUsable,
    CfdMomentumProblem::GravityNotUsable,
    CfdMomentumProblem::TimeStepNotUsable,
    CfdMomentumProblem::TransientInputsAreInconsistent,
    CfdMomentumProblem::ValueLengthWrong,
    CfdMomentumProblem::ValueNotFinite,
    CfdMomentumProblem::ConvectionRefusedTheInput,
    CfdMomentumProblem::DiffusionRefusedTheInput,
    CfdMomentumProblem::ResultIsNotFinite,
    CfdMomentumProblem::AssemblyIsInconsistent,
};

static_assert(kCfdMomentumProblems.size() == 16,
              "a problem was added or removed: update kCfdMomentumProblems, toString, and "
              "CfdMomentum_TheDeclaredPhaseOrderIsTheCheckOrder, which asserts the order between "
              "the phases a constructible input can reach");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdMomentumProblem problem) noexcept;

/// A refusal with its tag.
///
/// THE TAG IS THE POINT, and `std::expected` rather than `Result<T>` is what
/// makes it observable. `ResultIsNotFinite` judges an output, so no predictor
/// can reach it, and with a plain `Error` the enumerator would be observable
/// only through a message's wording -- against `CfdBoundary.hpp`'s own "a
/// message is not a tag" rule. `P19-DIV-001` recorded that as its L10;
/// `P19-CONV-001` resolved it; this milestone inherits the resolution rather
/// than the limitation (ADR-073 Decision 10).
struct CfdMomentumFailure {
    CfdMomentumProblem problem{};
    Error error{};
};

// ---------------------------------------------------------------------------
// What one assembly did
// ---------------------------------------------------------------------------

/// What one momentum assembly did, so a consumer need not re-derive it and an
/// evidence file need not guess.
///
/// THE TWO SUB-SUMMARIES ARE CARRIED VERBATIM, NOT RE-DERIVED. They already
/// hold the facts a reader of a momentum system needs and that nothing here
/// could state more accurately: `convection.backflowFaces` and
/// `diffusion.deferredFraction` are the two conditions under which the
/// assembled operator is NOT bounded, and
/// `diffusion.constantModeIsInTheNullSpace` says whether the steady operator
/// is singular on constants. Restating any of them here would be a second
/// source of truth for a number the sub-operator measured.
struct CfdMomentumSummary {
    std::size_t cells = 0;
    /// Which component's system this is.
    VectorComponent component = VectorComponent::X;

    /// True exactly when a time step was supplied, so the transient term was
    /// assembled. FALSE MEANS THE STEADY FORM.
    bool transientTermApplied = false;
    /// True exactly when a non-empty pressure gradient was supplied.
    ///
    /// FALSE MEANS THE ASSEMBLED SOURCE CARRIES NO PRESSURE TERM, which is a
    /// momentum equation missing one of its two driving forces. Reported for
    /// the reason `CfdDiffusionSummary::correctionApplied` is: an omission a
    /// caller chose is legitimate, and an omission a caller cannot see is not.
    bool pressureGradientApplied = false;
    /// True exactly when a gravity record was supplied.
    bool gravityApplied = false;

    /// The smallest and largest `rho V / dt` over the mesh, in kg/s. Both zero
    /// when the form is steady.
    ///
    /// TYPED, AND THE UNIT IS THE POINT: the transient coefficient is a MASS
    /// FLOW RATE, the same quantity as `phi` and as diffusion's `mu A / d`, so
    /// these two numbers are directly comparable with
    /// `CfdConvectionSummary::inflow` and say how heavily the time step weighs
    /// against the flow. The static assertions above prove the unit rather
    /// than asserting it in a comment.
    MassFlowRate smallestTransientCoefficient{};
    MassFlowRate largestTransientCoefficient{};

    /// Rows that are NOT weakly diagonally dominant: `|a_ii|` is less than the
    /// sum of `|a_ij|` over the rest of the row.
    ///
    /// A MEASUREMENT, NOT A GUARANTEE, AND NOT AN ACHIEVEMENT. Nothing here
    /// makes a row dominant: there is no diagonal floor, no clipping and no
    /// artificial viscosity (ADR-073 Decision 9). Dominance fails legitimately
    /// under `Central`, and under `Upwind` wherever backflow at a zero-gradient
    /// boundary subtracts from a diagonal -- which is
    /// `convection.backflowFaces`, carried below.
    ///
    /// IT IS REPORTED BECAUSE THE CHECKLIST ASKS FOR NO HIDDEN STABILIZATION
    /// AND THIS IS WHAT A STABILIZED OPERATOR WOULD HIDE. A stabilized
    /// assembly reads zero here on a mesh where an honest one does not, so the
    /// number is evidence about the assembly and not about the mesh. It is
    /// measured from the matrix this function returns, so it cannot disagree
    /// with it.
    std::size_t rowsNotDiagonallyDominant = 0;

    /// The convection sub-assembly's own summary, verbatim.
    CfdConvectionSummary convection{};
    /// The diffusion sub-assembly's own summary, verbatim.
    CfdDiffusionSummary diffusion{};

    friend bool operator==(const CfdMomentumSummary&, const CfdMomentumSummary&) = default;
};

/// One assembled component system: `A U = b`.
struct CfdMomentumSystem {
    /// `A`, in kg/s, square and `cells` by `cells`.
    ///
    /// `A = A_transient + A_convection + A_diffusion`, accumulated as
    /// `(conv + diff)` slot-wise with the transient diagonal added afterwards.
    /// THAT ORDER IS PART OF THE CONTRACT: it was measured at 78.06%
    /// bit-exact against a different grouping, so a caller reproducing the sum
    /// in another order will not get this matrix back bit-for-bit.
    ///
    /// NOT SYMMETRIC, and no symmetry tolerance is passed to `fromCsr`.
    /// Diffusion's part is symmetric to the last bit and convection's is
    /// asymmetric by construction (`ADR-072` Decision 3), so their sum is
    /// asymmetric wherever `phi` is not zero. Demanding symmetry of a momentum
    /// operator with convection in it would be wrong rather than merely slack.
    CfdSparseMatrix matrix;
    /// `b`, in SI newtons, one entry per cell.
    ///
    /// The five terms, summed in THIS DECLARED ORDER:
    ///
    /// ```text
    /// 1  b_convection      prescribed-inflow contributions
    /// 2  b_diffusion       prescribed values, symmetry cross terms, deferred
    ///                      correction when supplied
    /// 3  rho V / dt * U^n  the previous time level, when transient
    /// 4  -V (grad p)       when a pressure gradient was supplied
    /// 5  rho V g           when a gravity record was supplied
    /// ```
    ///
    /// Measured at 57.45% bit-exact against the reverse order, worst relative
    /// `3.044e-13`, so the order is stated rather than left to chance. It is
    /// NOT the most accurate order -- summing by ascending magnitude would be,
    /// and agrees with this one on 68.64% -- and claiming otherwise would be a
    /// claim no measurement supports.
    std::vector<double> source;
    CfdMomentumSummary summary;
};

/// Everything the assembly needs that is not geometry.
struct CfdMomentumInput {
    /// The one density, `rho`. `ADR-058` excludes a variable density from P19
    /// v1 and `ADR-028` forbids this module from holding one: it comes through
    /// `features::requireIncompressibleFlowProperties` from the P15 material.
    ///
    /// IT ENTERS THE TRANSIENT TERM AND THE BODY FORCE ONLY. Convection does
    /// not take it, because `phi` already carries it.
    Density density{};
    /// The one dynamic viscosity, `mu`, passed to `assembleCfdDiffusion`.
    DynamicViscosity viscosity{};

    /// Which component's system this is. A velocity assembly is three calls,
    /// one per component, exactly as `ADR-066` and `ADR-069` made diffusion's.
    VectorComponent component = VectorComponent::X;

    /// `phi` at every face, in SI (kg/s), one per face, in `FaceIndex` order,
    /// internal faces first, SIGNED OUT OF THE OWNER. `cfdMassFlux` produces
    /// exactly this.
    std::span<const double> faceFlux{};
    /// Which scheme forms an internal face's convected value. Boundary faces
    /// are unaffected -- see `CfdConvectionScheme`.
    CfdConvectionScheme scheme = CfdConvectionScheme::Upwind;

    /// The CURRENT outer-iteration velocity of every component,
    /// component-blocked per `ADR-066` (`comp * cells + cell`), so `3 * cells`
    /// entries.
    ///
    /// REQUIRED ONLY WHEN A `Symmetry` FACE IS PRESENT, which is
    /// `assembleCfdDiffusion`'s requirement and is passed straight through:
    /// a slip wall's cross terms are a real part of the flux, and this is the
    /// field they are taken from.
    ///
    /// NOT THE PREVIOUS TIME LEVEL. See `previousVelocity`.
    std::span<const double> currentVelocity{};

    /// The gradient of THIS component at each face, one per face, in
    /// `FaceIndex` order. Empty omits diffusion's non-orthogonal correction,
    /// which is then reported through `summary.diffusion.correctionApplied`.
    ///
    /// WITHOUT IT THE DIFFUSION TERM IS THE IMPLICIT ARM ONLY, and on a
    /// non-orthogonal mesh that is not a complete Laplacian -- `ADR-069`'s
    /// limitation, carried here unchanged, with `ADR-063` finding F1's
    /// measurement of P16's reference meshes at 31 to 66 degrees mean
    /// non-orthogonality behind it.
    std::span<const VelocityGradient3> faceGradient{};

    /// The time step. PRESENT MEANS TRANSIENT and requires
    /// `previousVelocity`; absent means the STEADY FORM and requires
    /// `previousVelocity` to be empty.
    ///
    /// BACKWARD EULER, AND THERE IS NO SELECTOR. `ADR-058` fixes the scheme
    /// and there is one, so a field with one legal value would be a false
    /// capability.
    std::optional<Time> timeStep{};

    /// The velocity at the PREVIOUS TIME LEVEL, `U^n`, component-blocked as
    /// `currentVelocity` is, so `3 * cells` entries. Empty for the steady
    /// form.
    ///
    /// A DIFFERENT ARRAY FROM `currentVelocity`, AND THE NAMING IS
    /// LOAD-BEARING. Both are `std::span<const double>` of the same length in
    /// the same layout, so the type system cannot tell them apart. Passing the
    /// CURRENT field here makes the transient term's two halves cancel: the
    /// diagonal still carries `rho V / dt` and the source carries
    /// `rho V / dt * U_current`, so the system becomes the steady one
    /// perturbed toward its own current state -- which CONVERGES TO THE
    /// CORRECT STEADY ANSWER and would pass every steady assertion in the
    /// suite. `CfdMomentum_TheTransientSourceCarriesThePreviousFieldAndNotTheCurrentOne`
    /// is the one test that can see this, and it is why that test exists
    /// (ADR-073 Decision 4).
    std::span<const double> previousVelocity{};

    /// `grad p` at every cell, in SI (Pa/m), one per cell, in `CellIndex`
    /// order. Empty omits the pressure term, which is then reported through
    /// `summary.pressureGradientApplied`.
    ///
    /// CALLER-SUPPLIED, AND THE OPERATOR DOES NOT CHOOSE THE RECONSTRUCTION.
    /// `reconstructCfdPressureGradient` produces a `CfdPressureGradientField`
    /// whose `at(cell)` fills this array; which of `ADR-070`'s two methods
    /// made it is the analysis's decision and not this operator's
    /// (ADR-073 Decision 5). Momentum validates this array's length and
    /// finiteness and NOTHING ELSE: its accuracy is the caller's, and neither
    /// this array nor `previousVelocity` carries a mesh stamp, so a field of
    /// the right length from another mesh is accepted (ADR-073 L5, L6).
    std::span<const PressureGradient3> pressureGradient{};

    /// The body force, `rho g`. ABSENT MEANS NO GRAVITY.
    ///
    /// NOT AN `Acceleration3` WITH A ZERO DEFAULT. `CfdData.hpp`: "NO GRAVITY
    /// IS THE ABSENCE OF THIS RECORD, not a zero vector: a zero vector would
    /// be a second spelling of none." A zero vector is refused rather than
    /// treated as absence.
    ///
    /// ONLY `acceleration` IS READ. `referenceHeight` belongs to prescribed
    /// boundary pressures and reading it here would apply the hydrostatic
    /// field twice (ADR-073 Decision 6).
    ///
    /// THIS IS GRAVITY AND NOT BUOYANCY, which `ADR-058` refuses from P19 v1:
    /// buoyancy needs density to vary with temperature and there is one
    /// constant density. A constant `rho g` at a constant density is
    /// absorbable into a modified pressure, so in a domain no boundary
    /// pressure fixes it changes no velocity field at all. That is a property
    /// of the equation rather than a defect, and
    /// `CfdMomentum_GravityAndAHydrostaticPressureGradientCancel` asserts it
    /// instead of describing it.
    std::optional<CfdGravity> gravity{};
};

// ---------------------------------------------------------------------------
// The assembly
// ---------------------------------------------------------------------------

/// Assembles the momentum equation for one component.
///
/// `A = A_transient + A_convection + A_diffusion` and
/// `b = b_conv + b_diff + b_transient + b_pressure + b_gravity`, in the orders
/// `CfdMomentumSystem` declares.
///
/// THE SUB-OPERATORS ARE CALLED, NOT RE-DERIVED, AND THEIR PATTERNS ARE
/// VERIFIED IDENTICAL RATHER THAN ASSUMED SO (ADR-073 Decisions 1 and 2).
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdMomentumSystem, CfdMomentumFailure>
assembleCfdMomentum(const CfdTopology& topology, const CfdGeometry& geometry,
                    const PreparedCfdBoundary& boundary, const CfdMomentumInput& input);

/// The refusal `assembleCfdMomentum` would give, with its tag, or none if it
/// would succeed.
///
/// RECORDED FOR WHAT IT IS WORTH: this shares one decision path with the
/// assembly, so their agreement cannot fail -- the limitation `P19-DIFF-001`
/// recorded as its L8, `P19-GRAD-001` as its L9 and `P19-DIV-001` and
/// `P19-CONV-001` as their own. It is here because a caller may legitimately
/// ask whether an input is usable without paying for the answer.
///
/// IT CANNOT SEE `ResultIsNotFinite`, which judges an output this function
/// never computes, and it cannot see `AssemblyIsInconsistent`, which judges
/// the assembly's own arrays.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdMomentumFailure>
cfdMomentumFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                   const PreparedCfdBoundary& boundary, const CfdMomentumInput& input);

/// The problem alone, allocation-free.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdMomentumProblem>
cfdMomentumProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                   const PreparedCfdBoundary& boundary, const CfdMomentumInput& input) noexcept;

// ---------------------------------------------------------------------------
// The measured bounds
// ---------------------------------------------------------------------------

/// The bound the row-sum additivity identity holds to, RELATIVE TO THE ROW'S
/// OWN SCALE.
///
/// In real arithmetic, per cell,
///
/// ```text
/// rowSumSi(A_momentum, c)
///     == rowSumSi(A_conv, c) + rowSumSi(A_diff, c) + rho V_c / dt
/// ```
///
/// `1e-13`, AND THE REASON IS MEASURED, NOT CHOSEN. Both sides sum the same
/// addends in different orders -- the left over combined coefficients in
/// ascending column order, the right over each operator's row separately --
/// and the same addends in a different order are a different double. Over
/// 50,000 random rows, bit-exact on 47.39%, worst departure `1.085e-14`
/// relative to `max |a_ij|` in the row. This bound is about a factor of nine
/// above that worst case: ONE decade of margin, not two.
///
/// AN EARLIER DRAFT SET IT AT `1e-12`, AND ITS OWN COMMENT CONDEMNED THAT
/// VALUE. The paragraph below rejects the sum-relative figure of `5.264e-12`
/// as "two decades slacker than the operator needs" -- and `1e-12` is 92 times
/// `1.085e-14`, which is two decades. So the sentence described the constant it
/// had chosen rather than the one it was rejecting, and the "factor of 500"
/// comparison between Q3a and Q3b had been silently transferred to a choice it
/// did not justify. An independent review did the arithmetic. The constant is
/// tightened to the figure Q3b supports and the suite re-run; no assertion
/// needed the slack.
///
/// RELATIVE TO THE ROW SCALE AND NOT TO THE SUM, WHICH IS THE WHOLE POINT OF
/// THE CONSTANT. The same measurement relative to the SUM gives a worst
/// `5.264e-12`, a factor of 500 looser -- because a momentum row with no
/// prescribed-value face nearly cancels, so dividing by the sum measures the
/// cancellation and not the assembly. A tolerance taken from THAT figure would
/// be two decades slacker than the operator needs and would hide a formula
/// error (ADR-073's Q3a and Q3b, both published for this reason).
inline constexpr double kCfdMomentumRowSumTolerance = 1.0e-13;

/// The bound the hydrostatic cancellation holds to, relative to the magnitude
/// of the gravity term.
///
/// A fluid at rest stays at rest: with a pressure field whose gradient is
/// exactly `rho g`, the pressure and gravity terms must cancel and the
/// momentum source must vanish.
///
/// `1e-14`, AND A BIT-ZERO CLAIM WOULD BE FALSE. The two terms are the same
/// product in different groupings -- `-(V * fl(rho * g))` against
/// `((rho * V) * g)` -- because the gradient arrives from a caller and the body
/// force is formed here. Measured over 200,000 random triples: exactly `0.0` on
/// 65.05%, worst `|residual| / |gravity term|` of `3.649e-16`.
///
/// THE BOUND IS A FACTOR OF 27 ABOVE THAT WORST MEASUREMENT -- 1.44 decades,
/// not two. An earlier draft said "two decades", which is arithmetically wrong
/// (two decades above `3.649e-16` is `3.6e-14`), and an independent review
/// caught it here and in ADR-073's Q2. The FIGURE is corrected rather than the
/// bound loosened to match the sentence: `1e-14` is the tighter of the two, and
/// a tolerance is not widened to make a claim about it come true.
///
/// AN EARLIER FORM OF THAT MEASUREMENT REPORTED A WORST RELATIVE DEPARTURE OF
/// `1.000e+00` and was an artefact of its own metric: a relative comparison
/// against exact zero is `1.0` for every non-zero residual, so the figure was
/// a number about nothing. Recorded because it looked like a catastrophic
/// failure of the physics and was a defect in the measure.
inline constexpr double kCfdMomentumHydrostaticTolerance = 1.0e-14;

} // namespace bettercad::cfd
