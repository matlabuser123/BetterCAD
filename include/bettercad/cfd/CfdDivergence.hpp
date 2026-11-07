#pragma once

// P19-DIV-001 — Divergence / Flux Operator. ADR-071.
//
// `integral_V div F dV = sum_f F_f . S_f` over the finite-volume topology, for
// two fluxes: a caller-owned face mass flow rate (ADR-066's `phi`), and a
// cell-centred velocity whose face value comes from ADR-067's frozen table.
//
// WHAT IS STORED IS THE INTEGRAL, NOT THE DIVERGENCE. The field holds the
// signed sum `sum_f ±(F_f . S_f)` per cell -- the LEFT-hand side of the identity
// above, equivalently the net outflow -- and divides by the cell volume on read.
// The conserved quantity is the integral, so storing it keeps a rounding step
// out of the conservation claim itself (ADR-071 Decision 1).
//
// THE TRAVERSAL IS A FACE-CENTRED SCATTER. One flux is computed per face, added
// to the owner and SUBTRACTED from the neighbour. Because `(-S) . U` is
// bit-for-bit `-(S . U)` -- 200,000 random triples, zero mismatches -- an
// internal face's two contributions sum to exactly `0.0`, so internal-face
// cancellation is a property of the traversal and not a tolerance. The
// Green-Gauss gradient gathers instead, and ADR-070 had to make the
// interpolation weight the owner's so its two visits could not disagree; a
// scatter removes the question rather than answering it (Decision 2).
//
// GLOBAL CONSERVATION IS MEASURED, NOT PROMISED. Summing the per-cell integrals
// over the mesh does NOT reproduce the boundary sum bit-for-bit, and the first
// draft of ADR-071 predicted that it would: each cell accumulates its own faces
// before the two sides of an internal face meet, and `(a + x) + (b - x)` is not
// `a + b`. Measured over 20,000 meshes the imbalance is bit-zero on 40.34% and
// at worst `1.125e-16` of the accumulated flux magnitude, so the operator
// reports the difference and claims nothing about it vanishing (Decision 3).
//
// WHAT THIS DOES NOT DO. There is no convection scheme -- P19-CONV-001 defines
// `phi` and its interpolation and is not authorized, so this operator consumes a
// `phi` somebody else produced. There is no solve, so nothing here is shown to
// produce a flow field. There is no least-squares form: `trace(grad U)` from
// ADR-070 would be linear-exact on any mesh where this surface sum is exact only
// where its interpolation is, and building it would duplicate ADR-070 to answer
// a question the checklist does not ask. And there is no quality threshold:
// ADR-063 finding F1 reserves that to P19-VALID-001 and the owner, so imbalance
// and skewness are REPORTED and only a non-finite result is refused.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

/// Why a divergence cannot be taken.
///
/// Declared in PHASE ORDER, and the order is asserted BETWEEN PHASES: an input
/// with faults in two different phases reports the earlier phase.
///
/// THE TWO ENTRY POINTS REACH DIFFERENT SUBSETS, and each enumerator says
/// which. The mass-flux path reads a face array and no boundary at all, so it
/// cannot report the boundary phases; the velocity path reads cell values and
/// the `U` column of ADR-067's table, so it can.
///
/// WITHIN A PHASE THAT WALKS THE BOUNDARY, THE FIRST OFFENDING FACET DECIDES
/// and not the enum order, which is what `CfdBoundary.hpp` and
/// `CfdGradient.hpp` both say of their own facet refusals, and for the same
/// reason: claiming an order the code does not have would be a false claim
/// about a diagnostic.
enum class CfdDivergenceProblem : std::uint8_t {
    /// The metrics describe a different mesh generation from the topology.
    /// Checked first because every later phase reads both. BOTH PATHS.
    GeometryDescribesAnotherTopology,
    /// The prepared boundary was built against a different generation.
    /// VELOCITY PATH ONLY -- the mass-flux path takes no boundary.
    BoundaryDescribesAnotherTopology,
    /// The values are not the length the entry point requires: one per FACE for
    /// the mass flux, which is a `Face`-located variable in ADR-066's table, or
    /// three per CELL in that table's component-blocked layout for the velocity.
    ///
    /// ONE ENUMERATOR FOR TWO SHAPES, because the fault is the same fault and
    /// the message carries which was expected. BOTH PATHS.
    ValueLengthWrong,
    /// A supplied value is NaN or infinite. Checked AFTER the length, so a wrong
    /// length is reported as a length and not as whatever happens to sit in it.
    /// BOTH PATHS.
    ValueNotFinite,
    /// A boundary face gives the velocity the `FixedValue` policy and the
    /// prepared boundary holds no velocity for it. VELOCITY PATH ONLY.
    ///
    /// AN INCONSISTENCY IN THAT VALUE, not a zero: defaulting to zero would
    /// invent a wall condition nobody asked for, which P19-DIFF-001 and
    /// P19-GRAD-001 both refused for the same reason.
    ///
    /// UNREACHABLE FROM ANY CONSTRUCTIBLE INPUT, and kept for the reason
    /// `CfdGradient.hpp` records of its own copy: `PreparedCfdBoundary` has no
    /// public constructor, and the one assembler sets the velocity whenever the
    /// policy is `FixedValue` or refuses by name. The state is excluded by
    /// ADR-067's table and by an assembler in another module, both of which this
    /// operator only reads, so the refusal is a defence against that table
    /// moving underneath it.
    PrescribedValueMissing,
    /// A boundary face gives the VELOCITY the `Computed` policy, which is an
    /// outcome of a solve and not a boundary condition. VELOCITY PATH ONLY.
    ///
    /// NEEDED TO MAKE THE SWITCH TOTAL, which is a stronger reason than defence.
    /// `CfdBoundaryPolicy` has four values and the `U` column of ADR-067's table
    /// uses three; a total switch must still answer for the fourth, and the only
    /// correct answer is a refusal. Writing a default that fell through to the
    /// owner's own velocity would silently admit flow through a boundary whose
    /// flux is an unknown.
    ///
    /// THE MASS-FLUX PATH HAS NO SUCH REFUSAL, AND MUST NOT. `phi` IS `Computed`
    /// at both pressure boundaries, because at a static-pressure inlet "the flow
    /// rate is an OUTCOME of the solve, not an input" (ADR-058) -- and by the
    /// time anyone takes a divergence the solve has produced it. The caller
    /// supplies that face's value like any other. A policy says where a value
    /// comes from, not whether a sum may be taken over it (ADR-071 Decision 5).
    VelocityPolicyIsAnOutcome,
    /// The computed result is NaN or infinite.
    ///
    /// THE ONLY PHASE THAT JUDGES THE ANSWER, and it exists because
    /// P19-GRAD-001's adversarial review found its absence: requiring every
    /// INPUT to be finite admits `1.8e308`, from which a surface sum can
    /// overflow to infinities of each sign, cancel to a NaN, pass a positive
    /// volume check, and reach a caller with no error at all. `thermal` and
    /// `structural` both refuse a non-finite result rather than propagate it,
    /// and so does ADR-070. Built in here from the start rather than found
    /// again. BOTH PATHS.
    ///
    /// AND IT IS THE ONE ENUMERATOR A CALLER CANNOT READ AS A TAG. An
    /// independent review established this and it is recorded rather than
    /// papered over: the two `diverge*` entry points return a plain `Error`,
    /// the predictors cannot reach this phase at all because they judge an
    /// output they never compute, and so this value is observable only through
    /// the diagnostic's wording. `CfdBoundary.hpp` states the house rule it
    /// sits against -- "a message is not a tag" -- and answers it with
    /// `bindCfdBoundaryFailure`. There is no `cfdDivergenceFailure`, because
    /// adding one is new public API that this milestone's checklist does not
    /// contain. The CONDITION is reachable and tested; the TAG is not
    /// observable, and the test can therefore pin only that the message names
    /// the cell. Recorded as a limitation.
    DivergenceIsNotFinite,
};

/// Every problem, in phase order.
inline constexpr std::array<CfdDivergenceProblem, 7> kCfdDivergenceProblems{
    CfdDivergenceProblem::GeometryDescribesAnotherTopology,
    CfdDivergenceProblem::BoundaryDescribesAnotherTopology,
    CfdDivergenceProblem::ValueLengthWrong,
    CfdDivergenceProblem::ValueNotFinite,
    CfdDivergenceProblem::PrescribedValueMissing,
    CfdDivergenceProblem::VelocityPolicyIsAnOutcome,
    CfdDivergenceProblem::DivergenceIsNotFinite,
};

static_assert(kCfdDivergenceProblems.size() == 7,
              "a problem was added or removed: update kCfdDivergenceProblems, toString, and "
              "CfdDivergence_TheDeclaredPhaseOrderIsTheCheckOrder, which asserts the order "
              "between the phases a constructible input can reach");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdDivergenceProblem problem) noexcept;

/// What a divergence measured about itself.
///
/// EVERY MEMBER HERE IS A COUNT OR DIMENSIONLESS, deliberately. The conservation
/// numbers are fluxes, so they are typed accessors on the field rather than
/// doubles in a struct shared by two instantiations -- ARCHITECTURE.md's rule
/// against a unitless engineering API, kept by putting the dimensioned answers
/// where the dimension is known.
struct CfdDivergenceSummary {
    /// Cells divided. Every cell the topology holds.
    std::size_t cells = 0;
    /// The faces the scatter walked, split as the topology splits them.
    std::size_t internalFaces = 0;
    std::size_t boundaryFaces = 0;

    /// VELOCITY PATH ONLY: boundary faces by the policy the `U` column gives
    /// them. The three sum to `boundaryFaces`, because `Computed` is refused and
    /// ADR-067's table has no fourth value down that column. All zero on the
    /// mass-flux path, which consults no policy.
    std::size_t fixedValueFaces = 0;
    std::size_t zeroGradientFaces = 0;
    std::size_t symmetryFaces = 0;

    /// VELOCITY PATH ONLY: the worst `|skewness|` over the internal faces whose
    /// value was interpolated. Zero on the mass-flux path, which interpolates
    /// nothing because `phi` is already a face quantity.
    ///
    /// THIS IS THE MEASURED BOUND ON THE OPERATOR'S OWN ERROR. With the TRUE
    /// face value the surface sum is linear-exact -- measured to `2.966e-16` over
    /// 20,000 random tetrahedra -- so every departure from exactness belongs to
    /// the interpolation, and the interpolation is exact exactly where skewness
    /// is zero.
    double worstSkewness = 0.0;

    /// `|globalNetFlux - netBoundaryFlux| / sum_f |flux_f|`, dimensionless.
    ///
    /// THE NUMBER TO JUDGE, and the denominator is the point: the scale a
    /// floating-point sum's error belongs to is the magnitude it accumulated,
    /// not the answer it arrived at. Stating it against the answer is
    /// P19-GRAD-001's finding r8, and the first draft of this milestone's own
    /// experiment made that mistake and reported `9.992e+284` for arithmetic
    /// that was correct to half an eps.
    ///
    /// Zero when nothing was accumulated, which is a statement about an empty or
    /// exactly-cancelling input and not about perfect conservation.
    double relativeImbalance = 0.0;

    // THERE IS NO `internalCancellationIsExact` FLAG HERE, AND THE REASON IS
    // WORTH STATING, because the first draft of this header carried one.
    //
    // The operator computes ONE flux per face and adds `+flux` to the owner and
    // `-flux` to the neighbour, so a flag measuring whether those two sum to
    // zero would be asking whether `x + (-x) == 0.0` for a finite `x`. That is
    // true for every finite double, always: the flag could not read false, and
    // a summary member that cannot vary is decoration that reads as evidence.
    // It is the tautological-check defect P19-DIFF-001's review found in a
    // cardinality test, and it was caught here before the code existed.
    //
    // The cancellation is STRUCTURAL (ADR-071 Decision 2) and the suite pins it
    // where a claim like this belongs: directly, by reading an internal face's
    // two contributions off a two-cell field and requiring them to sum to
    // exactly zero, and by the experiment that measured `(-S) . U == -(S . U)`
    // over 200,000 random triples with zero mismatches. Neither of those is a
    // flag the operator carries about itself.

    /// The worst `|sum_f S_f| / sum_f |S_f|` over the cells: how well a closed
    /// cell's surface vectors cancel, dimensionless, zero for perfect closure.
    ///
    /// THIS IS THE MEASURED BOUND ON THE CONSTANT-MODE RESIDUAL, and it is a
    /// NUMBER rather than a boolean on purpose. `sum_f c . S_f` vanishes for a
    /// constant `c` because `sum_f S_f` does, so the closure is what limits how
    /// exactly the operator reproduces a divergence-free uniform field.
    ///
    /// A `constantModeIsExact` BOOLEAN WAS WRITTEN HERE FIRST AND WITHDRAWN,
    /// because this milestone's own experiment showed it could not be derived
    /// from the closure the way ADR-070's was. Over 20,000 random tetrahedra
    /// `sum_f S_f` is bit-zero on 4.86% and the constant-field divergence is
    /// bit-zero on 24.14% -- so the two are not the same set in EITHER
    /// direction: a dot-product accumulation can cancel to zero on a cell whose
    /// closure is not bit-zero, and need not on one where it is. A flag claiming
    /// bit-exactness from the closure would therefore have been false, and
    /// deriving it honestly would mean running a probe constant inside the
    /// operator -- whose choice P19-GRAD-001 showed is treacherous, since the
    /// dyadic `17.25` cannot distinguish an unstable interpolation at all.
    ///
    /// So the mesh property is reported and the exactness claim is left to the
    /// suite, which tests the constant field directly with a constant chosen to
    /// discriminate.
    double worstClosure = 0.0;

    friend bool operator==(const CfdDivergenceSummary&, const CfdDivergenceSummary&) = default;
};

/// One divergence field: the face-integrated flux per cell, and what it measured.
///
/// ONE TEMPLATE, DISTINCT TYPES, for the reason `CartesianVector` and
/// `CfdGradientField` both give for being one: because the quantity is the
/// template argument, a mass-flow field cannot be passed where a volumetric one
/// is wanted, and the check is at compile time rather than a runtime tag a
/// caller can ignore.
///
/// THE VALUES ARE STORED IN SI AND TYPED ON READ, the split ADR-068 chose when
/// it put an expanded field in a caller-owned `std::span<double>`: the
/// arithmetic stays in the translation unit behind two explicit instantiations,
/// and no engineering API is unitless.
///
/// There is NO public constructor. The only door is a divergence, which means a
/// field that exists has been through the gate.
template <typename Q>
class CfdDivergenceField {
public:
    /// The face-integrated flux this field stores: `m^3/s` or `kg/s`.
    using Integral = Q;
    /// The divergence proper, `Integral / Volume`: `1/s` or `kg/(m^3 s)`.
    using Divergence = decltype(Q{} / Volume{});

    [[nodiscard]] std::size_t cells() const noexcept { return summary_.cells; }
    [[nodiscard]] const CfdDivergenceSummary& summary() const noexcept { return summary_; }

    /// The NET OUTFLOW from @p cell: `sum_f ±(F_f . S_f)`, the volume integral of
    /// the divergence. An empty optional for a cell this field does not hold --
    /// total, never undefined behaviour.
    ///
    /// Positive means net flow OUT, because the stored area vector points out of
    /// the owner (ADR-064) and the scatter preserves that sense.
    [[nodiscard]] std::optional<Q> at(CellIndex cell) const noexcept;

    /// The divergence at @p cell: `at(cell) / cellVolume(cell)`.
    ///
    /// THE VOLUME IS THE ONE THIS FIELD WAS BUILT WITH, stored per cell rather
    /// than re-read, so this accessor cannot disagree with the geometry the
    /// divergence was taken against even if a caller hands it a different one.
    [[nodiscard]] std::optional<Divergence> divergenceAt(CellIndex cell) const noexcept;

    /// The largest `|net outflow|` over the mesh. Zero for an empty field and
    /// for an exactly divergence-free one.
    [[nodiscard]] Q largestMagnitude() const noexcept;

    /// `sum` over BOUNDARY faces of the outward flux: what the whole mesh
    /// exchanges with the world outside it.
    [[nodiscard]] Q netBoundaryFlux() const noexcept;

    /// `sum` over CELLS of the net outflow.
    ///
    /// Equal to `netBoundaryFlux()` in exact arithmetic, because every internal
    /// face enters twice with opposite signs. NOT bit-equal in floating point --
    /// see `CfdDivergenceSummary::relativeImbalance`, and ADR-071 Decision 3 for
    /// why no stronger claim is made.
    [[nodiscard]] Q globalNetFlux() const noexcept;

    /// `globalNetFlux() - netBoundaryFlux()`: the measured conservation error,
    /// in the integrated unit. Signed, because which way it leans is information.
    [[nodiscard]] Q globalImbalance() const noexcept;

    friend bool operator==(const CfdDivergenceField&, const CfdDivergenceField&) = default;

private:
    CfdDivergenceField() = default;

    /// The only thing that may build one, defined in the translation unit that
    /// holds the gate.
    friend struct CfdDivergenceBuilder;

    /// One per cell, SI, in `CellIndex` order: the net outflow.
    std::vector<double> si_{};
    /// One per cell, SI, in `CellIndex` order: the volume `divergenceAt` divides
    /// by. Stored so the accessor cannot be given a different geometry.
    std::vector<double> volumeSi_{};
    double netBoundarySi_ = 0.0;
    double globalNetSi_ = 0.0;
    CfdDivergenceSummary summary_{};
};

/// The divergence of a cell-centred velocity, in `m^3/s` per cell.
using CfdVelocityDivergenceField = CfdDivergenceField<VolumetricFlowRate>;
/// The divergence of ADR-066's face `phi`, in `kg/s` per cell.
using CfdMassFluxDivergenceField = CfdDivergenceField<MassFlowRate>;

/// What to take the divergence of a face mass flow rate from.
///
/// A FACE ARRAY, BECAUSE `phi` IS A FACE VARIABLE. ADR-066's table puts the mass
/// variable at `CfdFieldLocation::Face` with one component, and this operator
/// sums what it is given: there is no interpolation, no area factor and no
/// boundary policy on this path, because `phi` is already the face-integrated
/// `rho (U . Sf)` (ADR-071 Decision 5).
struct CfdMassFluxDivergenceInput {
    /// `phi` at every face, in SI (kg/s), one per face, in `FaceIndex` order,
    /// INTERNAL FACES FIRST as ADR-064 numbers them.
    ///
    /// SIGNED OUT OF THE OWNER, matching the stored area vector: a positive value
    /// is flow from the owner to the neighbour across an internal face, and out
    /// of the fluid at a boundary face. The operator negates it for the
    /// neighbour and never re-derives the sign.
    std::span<const double> faceFlux{};
};

/// What to take the divergence of a cell-centred velocity from.
///
/// THE WHOLE VELOCITY, NOT ONE COMPONENT, and not because of a boundary subtlety
/// this time but because a divergence is intrinsically a vector operation: it
/// contracts the field against the face normal, so one component's slice could
/// not produce it. That also means ADR-070's `SymmetryNeedsAVectorField` refusal
/// has no analogue here, because there is no scalar entry point to protect.
struct CfdVelocityDivergenceInput {
    /// The cell velocities, in SI (m/s), COMPONENT-BLOCKED as ADR-066 lays them
    /// out: `component * cells + cell`, three times the cell count in all.
    std::span<const double> currentVelocity{};
};

/// Takes the divergence of a face mass flow rate.
///
/// No boundary argument, and that is Decision 5 rather than an omission.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdMassFluxDivergenceField>
divergeCfdMassFlux(const CfdTopology& topology, const CfdGeometry& geometry,
                   const CfdMassFluxDivergenceInput& input);

/// Takes the divergence of a cell-centred velocity field.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdVelocityDivergenceField>
divergeCfdVelocity(const CfdTopology& topology, const CfdGeometry& geometry,
                   const PreparedCfdBoundary& boundary, const CfdVelocityDivergenceInput& input);

/// The problem `divergeCfdMassFlux` would report, or none if it would succeed.
/// The same checks in the same order.
///
/// RECORDED FOR WHAT IT IS WORTH: this shares one decision path with the
/// divergence, so their agreement cannot fail -- the limitation P19-DIFF-001
/// recorded as its L8 and P19-GRAD-001 as its L9. It is here because a caller
/// may legitimately ask whether an input is usable without paying for the
/// answer, and because it pins the MESSAGE.
///
/// IT CANNOT SEE `DivergenceIsNotFinite`, which judges an output this function
/// never computes. A predictor that reported it would have to do the whole
/// scatter, which is the work the predictor exists to avoid.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdDivergenceProblem>
cfdMassFluxDivergenceProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                             const CfdMassFluxDivergenceInput& input) noexcept;

/// The problem `divergeCfdVelocity` would report, or none. Same caveats.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdDivergenceProblem>
cfdVelocityDivergenceProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                             const PreparedCfdBoundary& boundary,
                             const CfdVelocityDivergenceInput& input) noexcept;

} // namespace bettercad::cfd
