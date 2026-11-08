#pragma once

// P19-CONT-001 — Continuity Equation. ADR-074.
//
// `sum_f phi_f = 0` per cell, and `sum over boundary phi = 0` over the mesh, for
// ADR-066's face mass flux. This MEASURES continuity; it assembles nothing and
// solves nothing.
//
// IT COMPUTES TWO MEASURES IT DOES NOT DEFINE. `ADR-058` fixed them and
// `CfdData.hpp` declares them as `CfdMassBalance`, with the comment that they
// are fixed there "so that no later milestone can quietly report a different
// one":
//
//     maxCellContinuity   max over cells of |sum_f phi_f|, relative
//     globalImbalance     |sum over boundary faces of phi|, relative
//
// each divided by the boundary throughput `0.5 * sum |phi|`.
//
// THE `0.5` IS THERE BECAUSE THE THROUGHPUT IS THE ONE-WAY FLOW RATE. For a
// balanced field `inflow == outflow == 0.5 * sum |phi|`, so dividing by it
// states the imbalance per unit of flow the domain actually carries -- which is
// the quantity an engineer would compare against a percentage. A REVIEW
// OVERTURNED THE EXPLANATION THAT USED TO SIT HERE, which said the measure
// "would otherwise top out at 2 for the throughput rather than at 2 for the
// imbalance": without the `0.5` it tops out at ONE, so that sentence was not a
// reason for anything. The `[0, 2]` range is a CONSEQUENCE of the `0.5`, not
// its justification, and ADR-074 closed the same circle.
//
// Measured: a balanced field gives at worst `5.6167e-16` and an unbalanced one
// `1.9590`, so the denominator discriminates by fifteen orders of magnitude and
// the relative measure lies in `[0, 2]`, reaching 2 when all flow is one way.
//
// IT CONSUMES THE QUALIFIED DIVERGENCE AND DOES NOT RE-WALK THE INTERIOR.
// `divergeCfdMassFlux` (`ADR-071`) already produces the per-cell net outflow and
// the global sums; this evaluator calls it and walks BOUNDARY faces only, for
// the inlet/outlet and per-patch attribution the divergence cannot do because it
// takes no boundary. That is `ADR-073` Decision 1's pattern -- consume qualified
// operators rather than re-derive them -- applied to a measurement. The
// divergence field is carried verbatim on the report so the decomposition can be
// checked against it rather than believed.
//
// ------------------------------------------------------------------------------
// THE NAME COLLISION, WHICH WOULD HAVE MADE EVERY MASS BALANCE PASS
// ------------------------------------------------------------------------------
//
//     CfdDivergenceField::globalImbalance()  =  globalNetFlux() - netBoundaryFlux()
//                                               a FLOATING-POINT artifact of the
//                                               accumulation order.  ADR-071
//                                               measured it bit-zero on 40.34%,
//                                               worst 1.125e-16 relative
//
//     CfdMassBalance::globalImbalance        =  |sum over boundary phi| / throughput
//                                               THE PHYSICAL mass imbalance
//
// SAME NAME. DIFFERENT QUANTITY. Wiring the first into the second passes every
// mass balance, always, because the artifact stays near `1e-16` however badly
// the flow violates conservation -- and nothing in the type system objects,
// since both are a `double` behind a member called `globalImbalance`.
//
// This file never reads `CfdDivergenceField::globalImbalance()`. The physical
// measure is built from `netBoundaryFlux()` alone, and the artifact is carried
// under a DIFFERENT NAME, `CfdContinuitySummary::accumulationResidual`, so an
// assignment between the two cannot compile into silence. A mutation probe
// applies the swap and must kill the suite (`ADR-074` Decision 3).
//
// ------------------------------------------------------------------------------
// WHAT THIS DELIBERATELY DOES NOT DO
// ------------------------------------------------------------------------------
//
// NO INLET-VERSUS-OUTLET CHECK. `ADR-058`'s own section heading is "Mass balance
// means a discrete measure, not a sum of inlets and outlets", because with
// pressure-only boundaries the outflow is an OUTCOME of the solve and the check
// cannot be "outlet equals the prescribed inlet". Inflow and outflow are
// REPORTED, per patch and in total; the check is the global boundary imbalance.
// Nor is `outflow - inflow == netBoundaryFlux` claimed: it is bit-exact on 1.02%
// of balanced fields, so it is reported as agreeing to a measured bound.
//
// NO THROUGHPUT FLOOR AND NO ACCEPTANCE THRESHOLD. `CfdData.hpp` assigns both to
// `P19-MASSBAL-001` by name. The relative measures are therefore returned in an
// `std::optional`, present exactly when the throughput is strictly positive, and
// the absolute dimensioned quantities are always returned. Reporting `0.0` for a
// closed domain's relative imbalance would claim perfect balance where the
// measure is undefined, which reads as evidence and is the worse failure.
//
// NO `internalFacesCancelExactly` FLAG, and the reason is `ADR-071`'s own: the
// scatter computes one flux per face and adds `+flux` and `-flux`, so a flag
// asking whether those sum to zero asks whether `x + (-x) == 0.0` for a finite
// `x`, which is true for every finite double. A summary member that cannot read
// false is decoration that reads as evidence. Internal-face cancellation is
// instead measured through its AGGREGATE CONSEQUENCE -- `sum over cells of
// residual` reconciling with `netBoundaryFlux` -- which a scatter writing
// `+flux` twice would break grossly, and which a mutation probe confirms.
//
// NO TRANSIENT ACCUMULATION. `d(rho)/dt` is zero at the single constant density
// `ADR-058` allows in P19 v1, so steady and transient continuity coincide here.
//
// NO SOLVE, so nothing here is shown to produce a divergence-free field. `phi`
// is caller-supplied and its conservativeness is never assumed: a policy says
// where a value comes from, not whether a sum may be taken over it (`ADR-071`
// Decision 5).

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::cfd {

// ---------------------------------------------------------------------------
// Why a continuity evaluation cannot be made
// ---------------------------------------------------------------------------

/// Why continuity cannot be evaluated.
///
/// Declared in PHASE ORDER, and the order is asserted BETWEEN PHASES: an input
/// with faults in two different phases reports the earlier phase.
///
/// WITHIN A PHASE THAT WALKS THE BOUNDARY OR THE FACES, THE FIRST OFFENDING
/// ENTRY DECIDES and not the enum order -- the rule `CfdBoundary.hpp`,
/// `CfdGradient.hpp`, `CfdDivergence.hpp` and `CfdMomentum.hpp` all state of
/// their own facet refusals, for the same reason: claiming an order the code
/// does not have would be a false claim about a diagnostic.
enum class CfdContinuityProblem : std::uint8_t {
    /// The metrics describe a different mesh generation from the topology.
    /// Checked first because every later phase reads both.
    GeometryDescribesAnotherTopology,
    /// The prepared boundary was built against a different generation.
    ///
    /// THE CONDITIONAL FORM, as `P19-DIFF-001` corrected it and every operator
    /// since has inherited: a default-constructed source is compared against
    /// nothing, because `bindCfdBoundary` deliberately returns one and an
    /// unconditional comparison would refuse every seam-built boundary. The
    /// boundary-face count is compared unconditionally, because that always
    /// means something.
    BoundaryDescribesAnotherTopology,
    /// A face names an owner or neighbour the topology does not hold.
    ///
    /// THIS EVALUATOR DOES NOT INDEX BY OWNER OR NEIGHBOUR -- it walks cells and
    /// boundary faces -- but `divergeCfdMassFlux` does, and its own gate has no
    /// enumerator for this, so the guard lives here. `P19-DIV-001`'s review and
    /// then `P19-CONV-001`'s review each found an unguarded `SIZE_MAX` index
    /// reaching WRITES; the second was that correction REGRESSED one milestone
    /// later. It is not regressing a third time.
    TopologyNamesAnUnknownCell,
    /// A boundary face names a patch the topology does not hold.
    ///
    /// REACHED BY THIS EVALUATOR'S OWN INDEXING, unlike the cell guard above:
    /// the per-patch breakdown subscripts an array by `patchOf(...).value()`, so
    /// an invalid or out-of-range patch would be a write past the end.
    TopologyNamesAnUnknownPatch,
    /// `faceFlux` is not one value per FACE. `phi` is a `Face`-located variable
    /// in `ADR-066`'s table, with internal faces first as `ADR-064` numbers them.
    ValueLengthWrong,
    /// A supplied `phi` is NaN or infinite. Checked AFTER the length, so a wrong
    /// length is reported as a length and not as whatever happens to sit in it.
    ValueNotFinite,
    /// A computed aggregate is NaN or infinite.
    ///
    /// THE ONLY PHASE THAT JUDGES THE ANSWER, and it is REACHED THROUGH THE
    /// DIVERGENCE'S GATE rather than through this evaluator's own sums.
    ///
    /// AN EARLIER VERSION OF THIS COMMENT CLAIMED OTHERWISE -- that requiring
    /// every input to be finite "admits `1.7e308`, from which a boundary sum can
    /// overflow" -- AND THAT WAS FALSE. Writing the test for it is what showed
    /// so: `divergeCfdMassFlux` gates its own `netBoundaryFlux`,
    /// `globalNetFlux` AND its accumulated magnitude `sum_f |flux_f|`, and
    /// refuses any input where those overflow. Every quantity this evaluator
    /// computes is bounded by that magnitude --
    ///
    /// ```text
    /// inflow, outflow       <= sum over boundary |phi|  <= sum_f |flux_f|
    /// throughput             = 0.5 * sum over boundary |phi|
    /// every patch subtotal  <= its patch's |phi| sum
    /// netBoundaryFlux        IS the divergence's own, already gated
    /// largestCellResidual    a maximum over values the divergence gated
    /// ```
    ///
    /// -- so the input that would overflow one of them is refused by the
    /// sub-operator first.
    ///
    /// THE THREE RELATIVE RESIDUALS NEED THREE DIFFERENT ARGUMENTS, and an
    /// earlier version of this note gave one argument for all three and got
    /// `accumulationResidual` wrong:
    ///
    /// ```text
    /// directionalSplitResidual  both numerator terms are BOUNDARY sums, each
    /// patchSumResidual          bounded by the boundary magnitude, and they
    ///                           are the same net reconstructed two ways -- so
    ///                           NEARLY EQUAL rather than opposite, and the
    ///                           ratio is at most 2
    /// accumulationResidual      NOT bounded by that argument at all: its `a`
    ///                           is `globalNetFlux()`, a sum over every CELL.
    ///                           It is bounded because it is now the
    ///                           DIVERGENCE'S OWN gated figure, which that
    ///                           operator computes over the all-faces
    ///                           magnitude and refuses if non-finite
    /// ```
    ///
    /// While this file re-derived the third one over the boundary-only
    /// magnitude, the loop was REACHABLE and refused valid inputs -- so
    /// "unreachable" was false until that defect was fixed, and the reason was
    /// a bug rather than the inequalities. Recorded as `ADR-074` `L8`.
    ///
    /// THE LOOP IS KEPT AS AN UNREACHABLE DEFENCE and recorded as `L8` rather
    /// than justified by a route it does not have. It guards a promise ANOTHER
    /// module makes about its own gate, which is the same reason
    /// `CfdDivergence.hpp` keeps `PrescribedValueMissing` and `CfdMomentum.hpp`
    /// keeps its extrema check. The refusal itself IS reachable and IS tested --
    /// through the divergence's door, which this enumerator also carries,
    /// because that operator's channel is untagged and its own predictor cannot
    /// foresee a non-finite result.
    ResultIsNotFinite,
    /// A sub-operator refused an input this gate accepted, or accepted one it
    /// refused.
    ///
    /// UNREACHABLE WHILE `divergeCfdMassFlux`'S GATE KEEPS ITS PRESENT SHAPE,
    /// and kept because that is a promise another module makes. This evaluator
    /// predicts the divergence's refusal before calling it and maps every
    /// foreseeable one onto a phase above; if the divergence then refuses for a
    /// reason the predictor did not give, its gate has moved underneath this one
    /// and that is an internal failure rather than a caller's fault. Reported
    /// with `ErrorCode::Internal`.
    EvaluationIsInconsistent,
};

/// Every problem, in phase order.
inline constexpr std::array<CfdContinuityProblem, 8> kCfdContinuityProblems{
    CfdContinuityProblem::GeometryDescribesAnotherTopology,
    CfdContinuityProblem::BoundaryDescribesAnotherTopology,
    CfdContinuityProblem::TopologyNamesAnUnknownCell,
    CfdContinuityProblem::TopologyNamesAnUnknownPatch,
    CfdContinuityProblem::ValueLengthWrong,
    CfdContinuityProblem::ValueNotFinite,
    CfdContinuityProblem::ResultIsNotFinite,
    CfdContinuityProblem::EvaluationIsInconsistent,
};

static_assert(kCfdContinuityProblems.size() == 8,
              "a problem was added or removed: update kCfdContinuityProblems, toString, and "
              "CfdContinuity_TheDeclaredPhaseOrderIsTheCheckOrder, which asserts the order "
              "between the phases a constructible input can reach");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdContinuityProblem problem) noexcept;

/// A refusal and its tag.
///
/// TAGGED, FOR THE REASON `CfdMomentum.hpp` GIVES: `CfdDivergence.hpp` records
/// as a limitation that its `DivergenceIsNotFinite` is observable only through
/// the diagnostic's wording, because its entry points return a plain `Error`.
/// A tag is not a message.
struct CfdContinuityFailure {
    CfdContinuityProblem problem{};
    Error error{};
};

// ---------------------------------------------------------------------------
// Tolerances
// ---------------------------------------------------------------------------

/// The relative bound the per-cell continuity residual of a CONSERVATIVE field
/// holds to.
///
/// `1e-13`, AND THE REASON IS MEASURED. A field that is conservative by
/// construction -- a uniform velocity, `phi_f = rho (U_f . S_f)`, summed over a
/// closed cell -- has a residual that vanishes in real arithmetic, so whatever
/// is left is the floating-point floor. Measured over 20,000 random tetrahedra
/// per scale, seed `20261108`, before any of this existed:
///
/// ```text
/// metre-scale tets        worst 3.2789e-15   bit-zero 19.86%
/// millimetre-scale tets   worst 7.4598e-15   bit-zero 18.73%
/// ```
///
/// One decade above the worse of the two, not tight to it, because the
/// measurement is of random meshes and not of every mesh -- the reason
/// `kCfdConvectionRowSumTolerance` gives for the same shape of bound.
///
/// THE MESH CLOSURE DOES NOT BOUND IT. The worst `|sum_f S_f| / sum_f |S_f|` over
/// the same tetrahedra is `4.2666e-16` and `7.0816e-16`, about a tenth of the
/// residual, because the dot products round too and not only the area sum.
/// `ADR-071` recorded that bit-zero closure and a bit-zero constant-field
/// divergence are not the same set; this is that gap quantified in the direction
/// that matters here, and it is why the bound is not derived from the closure.
///
/// IT IS NOT A TOLERANCE ON CORRECTNESS, and it is not an acceptance threshold.
/// The identity is exact in real arithmetic; this is the floating-point slack.
/// Whether a flow's mass balance is ACCEPTABLE is `P19-MASSBAL-001`'s and
/// `P19-VALID-001`'s, with thresholds that are policy and not user settings
/// (`ADR-053`).
inline constexpr double kCfdContinuityCellTolerance = 1.0e-13;

/// The relative bound the two AGGREGATION reconstructions hold to.
///
/// `1e-14`, measured, and SEPARATE FROM THE CELL TOLERANCE ON PURPOSE. Two sums
/// a reader would take for identities are not:
///
/// ```text
/// (outflow - inflow)      vs netBoundaryFlux   worst 9.0866e-16, bit-exact  1.02%
/// sum of patch subtotals  vs netBoundaryFlux   worst 9.6334e-16, bit-exact  1.93%
/// ```
///
/// (the bit-exact rates are for balanced fields, which is the physical regime;
/// unbalanced fields reconstruct more often, 12.68% and 18.64%, because fewer
/// cancellations occur.)
///
/// ONE SHARED `1e-13` WOULD BE A HUNDRED TIMES LOOSER THAN THIS MEASUREMENT
/// SUPPORTS, so a formula error two orders of magnitude large in the directional
/// split or the per-patch aggregation would pass it. A tolerance that cannot
/// fail is not a gate -- the tautological-check defect `P19-DIFF-001`'s review
/// found, in a different dress.
inline constexpr double kCfdContinuityAggregationTolerance = 1.0e-14;

// ---------------------------------------------------------------------------
// Per-patch attribution
// ---------------------------------------------------------------------------

/// The mass flow across one boundary patch.
///
/// ONE RECORD PER PATCH THE TOPOLOGY HOLDS, in `PatchIndex` order, including
/// patches with no boundary faces -- which read all-zero rather than being
/// absent, so a caller indexing by patch cannot miss.
struct CfdPatchMassFlow {
    PatchIndex patch{};
    /// The boundary faces on this patch.
    std::size_t faces = 0;

    /// Faces by the SIGN of `phi`, never by the condition's name (`ADR-074`
    /// Decision 4). The three sum to `faces`.
    std::size_t inflowFaces = 0;
    std::size_t outflowFaces = 0;
    std::size_t zeroFluxFaces = 0;

    /// `sum` of `-phi` over the faces where `phi < 0`: mass entering. Never
    /// negative.
    MassFlowRate inflow{};
    /// `sum` of `phi` over the faces where `phi > 0`: mass leaving. Never
    /// negative.
    MassFlowRate outflow{};
    /// The single-pass `sum` of `phi` over this patch's faces, signed.
    ///
    /// NOT `outflow - inflow`, and the difference is measured rather than
    /// assumed away: see `kCfdContinuityAggregationTolerance`.
    MassFlowRate net{};

    /// The condition on this patch, present exactly when every one of its
    /// boundary faces carries the same `CfdBoundaryConditionKind`.
    ///
    /// AN EMPTY OPTIONAL MEANS *MIXED OR EMPTY*, and the two are told apart by
    /// `faces == 0`. (An earlier version of this sentence read "MIXED, NOT
    /// ABSENT, and a patch with no faces is also empty", whose first clause
    /// denies what its second states; a review said so.) The kind is reported
    /// so a reader can see a patch named
    /// `PressureInlet` carrying outflow -- which is legitimate, because
    /// `ADR-058` makes the flow rate at a pressure boundary an outcome, and
    /// `P19-CONV-001` already reports `backflowFaces`. THAT JUXTAPOSITION IS THE
    /// DIAGNOSTIC; nothing is refused for it.
    std::optional<CfdBoundaryConditionKind> kind{};

    friend bool operator==(const CfdPatchMassFlow&, const CfdPatchMassFlow&) = default;
};

// ---------------------------------------------------------------------------
// What one evaluation measured
// ---------------------------------------------------------------------------

/// What one continuity evaluation measured about itself.
///
/// EVERY MEMBER HERE IS A COUNT, A FLAG OR DIMENSIONLESS. The dimensioned
/// answers are typed accessors on the report, which is `CfdDivergence.hpp`'s own
/// split and `ARCHITECTURE.md`'s rule against a unitless engineering API kept by
/// putting the dimensioned answers where the dimension is known.
struct CfdContinuitySummary {
    /// Cells measured. Every cell the topology holds.
    std::size_t cells = 0;
    std::size_t internalFaces = 0;
    std::size_t boundaryFaces = 0;
    std::size_t patches = 0;

    /// Boundary faces by the SIGN of `phi`. The three sum to `boundaryFaces`.
    std::size_t inflowFaces = 0;
    std::size_t outflowFaces = 0;
    std::size_t zeroFluxFaces = 0;

    /// Boundary faces whose `phi` the table prescribes as `FixedValue 0` --
    /// `NoSlipWall`, `MovingWall` and `Slip` -- and the rest.
    ///
    /// BY THE TABLE AND NOT BY THE VALUE, which is the distinction that makes
    /// `closedDomain` worth having: `wallFaces` counts what the boundary
    /// CONDITION says, while `zeroFluxFaces` counts what the supplied `phi`
    /// actually is. A caller-supplied `phi` that is non-zero on a wall face
    /// makes them disagree, and that disagreement is visible rather than
    /// silently reconciled.
    std::size_t wallFaces = 0;
    std::size_t openFaces = 0;

    /// Every boundary facet is a wall by the table, so `openFaces == 0`.
    ///
    /// Checked EXACTLY and not to a tolerance: `ADR-067`'s table prescribes
    /// `phi = 0` at all three wall kinds and `ADR-072` writes `0.0` there, so a
    /// closed domain's boundary flux is BIT-ZERO -- measured at 0 non-zero nets
    /// in 5,000 trials. It is a separate question from the relative measures
    /// because a closed domain's throughput is also zero, so those are
    /// UNDEFINED rather than small.
    bool closedDomain = false;

    /// `|(outflow - inflow) - netBoundaryFlux| / sum |phi|` over the boundary.
    ///
    /// A MEASURED DEPARTURE, NOT AN ERROR. The two agree in real arithmetic and
    /// are bit-equal on only 1.02% of balanced fields; worst measured
    /// `9.0866e-16`. Reported so the associativity is visible instead of
    /// claimed away.
    double directionalSplitResidual = 0.0;

    /// `|sum over patches of net - netBoundaryFlux| / sum |phi|` over the
    /// boundary. The same kind of number as above: bit-equal on 1.93%, worst
    /// `9.6334e-16`.
    double patchSumResidual = 0.0;

    /// `CfdDivergenceSummary::relativeImbalance`, CARRIED VERBATIM:
    /// `|globalNetFlux - netBoundaryFlux|` over `sum_f |flux_f|` across ALL
    /// faces.
    ///
    /// THIS IS `ADR-071`'S ACCUMULATION ARTIFACT AND IT IS **NOT** THE MASS
    /// BALANCE. It is named `accumulationResidual` rather than
    /// `globalImbalance` precisely so it cannot be assigned to
    /// `CfdMassBalance::globalImbalance`, which is a different quantity of a
    /// different magnitude -- see this header's opening note. Each cell
    /// accumulates its own faces before the two sides of an internal face meet,
    /// and `(a + x) + (b - x)` is not `a + b`. `ADR-071` measured it bit-zero
    /// on 40.34% and at worst `1.125e-16` relative, and because the figure is
    /// now that operator's own, those measurements genuinely bound it.
    ///
    /// AN EARLIER VERSION RE-DERIVED IT HERE OVER THE BOUNDARY-ONLY MAGNITUDE,
    /// AND THAT WAS WRONG. The numerator's error scale is the ALL-FACES
    /// magnitude; dividing by the boundary's made the reported figure exceed its
    /// own `1e-14` bound by eight decades on a mesh with large interior and
    /// small boundary flux -- reaching exactly `1.0` for CORRECT arithmetic --
    /// and made it identically `0.0` in a closed domain, where the boundary
    /// magnitude is bit-zero. Two independent reviews found it and a third
    /// model confirmed it; `ADR-074` Decision 3 carries the figures.
    ///
    /// IT IS ALSO HOW INTERNAL-FACE CANCELLATION IS MEASURED. A scatter that
    /// wrote `+flux` to both sides instead of `+flux` and `-flux` would make
    /// this number order 1 rather than order `1e-16`, so it is a falsifiable
    /// consequence of the cancellation rather than a flag that cannot read
    /// false -- and with the correct denominator it is falsifiable in a CLOSED
    /// domain too, which it was not before.
    double accumulationResidual = 0.0;

    /// The worst `|sum_f S_f| / sum_f |S_f|` over the cells, carried verbatim
    /// from the divergence's own summary. Zero for perfect closure.
    double worstClosure = 0.0;

    friend bool operator==(const CfdContinuitySummary&, const CfdContinuitySummary&) = default;
};

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

/// One continuity evaluation: the residuals, the boundary attribution, and the
/// two measures `ADR-058` fixed.
///
/// There is NO public constructor. The only door is an evaluation, which means a
/// report that exists has been through the gate.
class BETTERCAD_CFD_EXPORT CfdContinuityReport {
public:
    [[nodiscard]] std::size_t cells() const noexcept { return summary_.cells; }
    [[nodiscard]] const CfdContinuitySummary& summary() const noexcept { return summary_; }

    /// The divergence this report was built from, VERBATIM.
    ///
    /// EXPOSED SO THE DECOMPOSITION CAN BE CHECKED AND NOT MERELY BELIEVED, the
    /// reason `CfdMomentumSummary` carries its two sub-summaries verbatim. A
    /// caller -- or a test -- can take `divergeCfdMassFlux` of the same input
    /// and require this to equal it.
    [[nodiscard]] const CfdMassFluxDivergenceField& divergence() const noexcept {
        return divergence_;
    }

    /// `sum_f phi_f` over @p cell's faces: the cell continuity residual, signed,
    /// positive for net outflow. An empty optional for a cell this report does
    /// not hold -- total, never undefined behaviour.
    ///
    /// THE DIVERGENCE'S OWN VALUE, not a second accumulation of the same faces.
    [[nodiscard]] std::optional<MassFlowRate> cellResidualAt(CellIndex cell) const noexcept;

    /// The largest `|cell residual|` over the mesh. Zero for an empty report and
    /// for an exactly conservative field.
    [[nodiscard]] MassFlowRate largestCellResidual() const noexcept;

    /// The cell attaining `largestCellResidual()`, or none for an empty report.
    ///
    /// THE FIRST SUCH CELL IN `CellIndex` ORDER when several tie, which is
    /// stated because a tie is reachable -- a uniform field on a symmetric mesh
    /// produces one -- and "the worst cell" would otherwise depend on traversal.
    [[nodiscard]] std::optional<CellIndex> worstCell() const noexcept;

    /// `sum` of `-phi` over boundary faces where `phi < 0`: mass entering the
    /// domain. Never negative.
    [[nodiscard]] MassFlowRate inflow() const noexcept;
    /// `sum` of `phi` over boundary faces where `phi > 0`: mass leaving. Never
    /// negative.
    [[nodiscard]] MassFlowRate outflow() const noexcept;

    /// The single-pass `sum` of `phi` over the boundary faces, signed: `ADR-058`'s
    /// measure (b) before normalisation, and the PHYSICAL global imbalance.
    [[nodiscard]] MassFlowRate netBoundaryFlux() const noexcept;

    /// `0.5 * sum |phi|` over the boundary faces: the throughput the two
    /// measures are relative to. Never negative.
    [[nodiscard]] MassFlowRate throughput() const noexcept;

    /// `ADR-058`'s two measures, each relative to `throughput()`.
    ///
    /// PRESENT EXACTLY WHEN `throughput()` IS STRICTLY POSITIVE. Absent for a
    /// closed domain and for any mesh whose boundary `phi` is all zero, because
    /// there is nothing to normalise against and the measures are UNDEFINED, not
    /// small. `CfdData.hpp` assigns the floor that would make them total to
    /// `P19-MASSBAL-001`, so this evaluator does not invent one (`ADR-074`
    /// Decision 9).
    ///
    /// `globalImbalance` here is `|netBoundaryFlux()| / throughput()` and lies in
    /// `[0, 2]`. It is NOT `divergence().globalImbalance()`.
    [[nodiscard]] std::optional<CfdMassBalance> massBalance() const noexcept;

    /// One record per patch the topology holds, in `PatchIndex` order.
    [[nodiscard]] std::span<const CfdPatchMassFlow> patches() const noexcept { return patches_; }

    friend bool operator==(const CfdContinuityReport&, const CfdContinuityReport&) = default;

private:
    /// THE ONLY CONSTRUCTOR, AND IT TAKES THE DIVERGENCE BY VALUE, so a report
    /// cannot exist without one.
    ///
    /// NOT A DEFAULT CONSTRUCTOR PLUS AN ASSIGNMENT, and that is the COMPILER'S
    /// correction rather than a preference. `CfdDivergenceField`'s own default
    /// constructor is private -- "there is NO public constructor; the only door
    /// is a divergence, which means a field that exists has been through the
    /// gate" -- so a default-constructed member here does not compile. The
    /// invariant that a divergence field has passed its gate therefore
    /// propagates into this type BY CONSTRUCTION: a `CfdContinuityReport` that
    /// exists holds a divergence that exists, and neither can be fabricated.
    explicit CfdContinuityReport(CfdMassFluxDivergenceField divergence)
        : divergence_(std::move(divergence)) {}

    /// The only thing that may build one, defined in the translation unit that
    /// holds the gate.
    friend struct CfdContinuityBuilder;

    CfdMassFluxDivergenceField divergence_;
    std::vector<CfdPatchMassFlow> patches_{};
    double inflowSi_ = 0.0;
    double outflowSi_ = 0.0;
    double netBoundarySi_ = 0.0;
    double throughputSi_ = 0.0;
    std::optional<CfdMassBalance> massBalance_{};
    std::optional<CellIndex> worstCell_{};
    double largestCellResidualSi_ = 0.0;
    CfdContinuitySummary summary_{};
};

// ---------------------------------------------------------------------------
// Input and entry points
// ---------------------------------------------------------------------------

/// What a continuity evaluation needs.
///
/// A FACE ARRAY AND NOTHING ELSE, because `phi` is a face variable and is
/// already the face-integrated `rho (U . S_f)`. There is no velocity entry
/// point: the checklist's `sum_f rho U_f . S_f` IS `phi`, and producing it from
/// a velocity is `cfdMassFlux`'s job (`ADR-072`), already qualified. A second
/// producer here would be the fourth private `phi` this module has seen.
struct CfdContinuityInput {
    /// `phi` at every face, in SI (kg/s), one per face, in `FaceIndex` order,
    /// INTERNAL FACES FIRST as `ADR-064` numbers them.
    ///
    /// SIGNED OUT OF THE OWNER, matching the stored area vector: positive is
    /// flow from the owner to the neighbour across an internal face, and OUT OF
    /// THE FLUID at a boundary face. Never re-derived here.
    std::span<const double> faceFlux{};
};

/// Evaluates continuity over @p topology.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdContinuityReport, CfdContinuityFailure>
evaluateCfdContinuity(const CfdTopology& topology, const CfdGeometry& geometry,
                      const PreparedCfdBoundary& boundary, const CfdContinuityInput& input);

/// The refusal `evaluateCfdContinuity` would give, with its tag, or none if it
/// would succeed.
///
/// RECORDED FOR WHAT IT IS WORTH: this shares one decision path with the
/// evaluation, so their agreement cannot fail -- the limitation `P19-DIFF-001`
/// recorded as its L8 and every cfd operator since has inherited. It is here
/// because a caller may legitimately ask whether an input is usable without
/// paying for the answer, and because it pins the MESSAGE.
///
/// IT CANNOT SEE `ResultIsNotFinite` OR `EvaluationIsInconsistent`, which judge
/// an output it never computes.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdContinuityFailure>
cfdContinuityFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary, const CfdContinuityInput& input);

/// The problem alone, allocation-free.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdContinuityProblem>
cfdContinuityProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary,
                     const CfdContinuityInput& input) noexcept;

} // namespace bettercad::cfd
