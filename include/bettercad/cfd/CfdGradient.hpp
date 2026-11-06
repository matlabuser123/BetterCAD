#pragma once

// P19-GRAD-001 — Gradient Reconstruction. ADR-070.
//
// Reconstructs a CELL-CENTRED gradient from cell values, by one of two methods,
// with the boundary face value taken from ADR-067's frozen policy table.
//
// WHY TWO METHODS, AND WHY LEAST SQUARES IS THE DEFAULT. Green-Gauss with the
// TRUE value at a face centroid is linear-exact on any mesh -- the identity
// `sum_f C_f (x) S_f = V I` is why, measured to 2.9e-16 over 20,000 random
// tetrahedra -- so all of its inexactness belongs to the INTERPOLATION that
// estimates the face value from the two cell values. That interpolation is
// exact exactly when the face is not skewed, and ADR-065 measured skewness as
// high as 6.5 on RM-MESH-03, where it costs 590% of the gradient's own
// magnitude on an otherwise well-shaped cell. Least squares interpolates
// nothing: it fits the gradient to the neighbour values at their own centroids,
// so it is linear-exact whatever the mesh looks like. Green-Gauss is kept
// because the checklist names it, because it is cheap, and because the
// difference between the two ON A REAL MESH is the only honest answer to "skew
// mesh behaviour" (ADR-070 Decision 2).
//
// WHAT THIS DOES NOT DO. It produces no FACE gradient, so nothing here is wired
// into `assembleCfdDiffusion` -- that operator already accepts a face gradient
// and needs no change, and interpolating cell gradients to faces is a coupling
// step this checklist does not contain. There is no skewness correction (it
// needs the gradient to compute the gradient), no limiter (there is no
// convection scheme to limit), and no mesh-quality threshold: ADR-063 finding
// F1 reserves that to P19-VALID-001 and the owner, so conditioning is REPORTED
// and only a numerically rank-deficient neighbourhood is refused.

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

/// Which reconstruction to use.
enum class CfdGradientMethod : std::uint8_t {
    /// `(1/V) sum_f phi_f S_f`, with `phi_f` interpolated on an internal face
    /// and taken from the boundary policy on a boundary face. Linear-exact only
    /// where the interpolation is, which is where skewness is zero.
    GreenGauss,
    /// Minimises `sum_N w_N (phi_N - phi_P - g . d_N)^2`. Linear-exact on any
    /// mesh, because it interpolates nothing.
    LeastSquares,
};

/// Both, in declaration order.
inline constexpr std::array<CfdGradientMethod, 2> kCfdGradientMethods{CfdGradientMethod::GreenGauss,
                                                                      CfdGradientMethod::LeastSquares};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdGradientMethod method) noexcept;

/// Why a gradient cannot be reconstructed.
///
/// Declared in PHASE ORDER, and the order is asserted BETWEEN PHASES: an input
/// with faults in two different phases reports the earlier phase.
///
/// WITHIN A PHASE THAT WALKS THE BOUNDARY, THE FIRST OFFENDING FACET DECIDES AND
/// NOT THE ENUM ORDER. `BoundaryDescribesAnotherTopology` is also emitted from
/// inside the prescribed-value loop, so a boundary missing a treatment at facet
/// 9 and missing a prescribed value at facet 3 reports the LATER enumerator.
/// `CfdBoundary.hpp` states the same thing about its own three facet refusals,
/// and for the same reason: claiming an order the code does not have would be a
/// false claim about a diagnostic. An independent review found this header
/// claiming it.
enum class CfdGradientProblem : std::uint8_t {
    /// The metrics describe a different mesh generation from the topology.
    /// Checked first because every later phase reads both.
    GeometryDescribesAnotherTopology,
    /// The prepared boundary was built against a different generation.
    BoundaryDescribesAnotherTopology,
    /// The cell values are not one per cell -- or, for the velocity entry
    /// point, not three per cell in ADR-066's component-blocked layout.
    CellValueLengthWrong,
    /// A cell value is NaN or infinite. Checked AFTER the length, so a wrong
    /// length is reported as a length and not as whatever happens to sit in it.
    CellValueNotFinite,
    /// A boundary face gives this variable the `FixedValue` policy and the
    /// prepared boundary holds no value for it.
    ///
    /// AN INCONSISTENCY IN THAT VALUE, not a zero. Defaulting to zero would
    /// invent a wall condition nobody asked for, which is the error
    /// P19-DIFF-001 refused for the same reason.
    ///
    /// UNREACHABLE FROM ANY CONSTRUCTIBLE INPUT, and an earlier version of this
    /// comment wrongly said "reachable only from a hand-built boundary". An
    /// independent review traced it: `PreparedCfdBoundary` has no public
    /// constructor, its only friends are `CfdBoundaryAssembler` and
    /// `prepareCfdBoundary`, `bindCfdBoundary` goes through the same assembler,
    /// and that assembler sets the value whenever the policy is `FixedValue` --
    /// refusing by name rather than leaving it empty if the value cannot be
    /// formed. So there is no door at all, not merely no document path.
    ///
    /// KEPT ANYWAY, AND THE REASON MATTERS. `CfdBoundary.hpp` records the
    /// project's policy that "an enumerator no input can produce is worse than
    /// no enumerator", and that was written about a state the DESIGN excludes.
    /// This one is excluded by ADR-067's table and by an assembler in another
    /// module, both of which this operator only reads. The refusal is a defence
    /// against that table changing under it, and it is documented as such
    /// rather than as something a test reaches.
    PrescribedValueMissing,
    /// A boundary face gives a SCALAR field the `Symmetry` policy.
    ///
    /// `Symmetry` decomposes a VECTOR: `U_b = U_P - (U_P . n) n`. It has no
    /// meaning for a pressure, and the scalar entry point holds one value per
    /// cell rather than three, so the projection has nothing to read.
    ///
    /// THIS REFUSAL EXISTS BECAUSE A REVIEW FOUND AN OUT-OF-BOUNDS READ WITHOUT
    /// IT. `boundaryValue` dispatched on the policy alone and never consulted
    /// which field it was serving, so a pressure face with this policy indexed
    /// `values[cells + cell]` -- past the end of a span that does no bounds
    /// checking. Unreachable today, down the same table as the enumerator above,
    /// and defended for the same reason.
    SymmetryNeedsAVectorField,
    /// A boundary face gives this variable the `Computed` policy, which is an
    /// outcome of a solve and not a boundary condition.
    ///
    /// UNREACHABLE FROM ANY CONSTRUCTIBLE INPUT, like the two above: `Computed`
    /// appears only in the `massFlux` column of ADR-067's table, which this
    /// operator never reads. An earlier version of this comment said "reached
    /// only by a hand-built boundary", which a review showed is not a door that
    /// exists. Refused rather than treated as a zero gradient, as a defence
    /// against that column moving.
    VariablePolicyIsAnOutcome,
    /// Least squares only: a cell's neighbourhood does not determine a gradient.
    ///
    /// Its normal matrix is numerically rank-deficient -- the neighbour
    /// directions do not span three dimensions to within
    /// `kCfdGradientConditioningLimit`. THIS IS NOT A MESH-QUALITY THRESHOLD: it
    /// is the floating-point line past which the computed direction is not
    /// determined by the data at all. An engineering threshold on conditioning
    /// is P19-VALID-001's, and `worstConditioning` is reported for it.
    NeighbourhoodIsDegenerate,
    /// The reconstructed gradient is not finite at some cell.
    ///
    /// LAST, BECAUSE IT IS THE ONLY PHASE THAT JUDGES THE OUTPUT. Every other
    /// refusal reads the inputs; this one reads the answer.
    ///
    /// IT EXISTS BECAUSE A REVIEW FOUND THE GREEN-GAUSS PATH RETURNING A NaN AS
    /// A SUCCESS. Phase 4 requires every cell value to be finite, which admits
    /// 1.8e308, and `CfdGeometry` accepts coordinates near the same magnitude --
    /// so a surface sum can overflow to an infinity of each sign, cancel to a
    /// NaN, pass `volume > 0`, and be handed back through `at()` and
    /// `largestMagnitude()` with no error at all. A tiny cell volume reaches the
    /// same place by division without any overflow in the sum.
    ///
    /// `thermal` and `structural` both refuse a non-finite gradient rather than
    /// propagate it; this operator now does the same. The least-squares path
    /// already refused such an input, but attributed it to
    /// `NeighbourhoodIsDegenerate` and printed a message about an eigenvalue
    /// ratio that was perfectly healthy -- a diagnostic that named the mesh when
    /// the fault was the field.
    GradientIsNotFinite,
};

/// Every problem, in phase order.
inline constexpr std::array<CfdGradientProblem, 9> kCfdGradientProblems{
    CfdGradientProblem::GeometryDescribesAnotherTopology,
    CfdGradientProblem::BoundaryDescribesAnotherTopology,
    CfdGradientProblem::CellValueLengthWrong,
    CfdGradientProblem::CellValueNotFinite,
    CfdGradientProblem::PrescribedValueMissing,
    CfdGradientProblem::SymmetryNeedsAVectorField,
    CfdGradientProblem::VariablePolicyIsAnOutcome,
    CfdGradientProblem::NeighbourhoodIsDegenerate,
    CfdGradientProblem::GradientIsNotFinite,
};

static_assert(kCfdGradientProblems.size() == 9,
              "a problem was added or removed: update kCfdGradientProblems, toString, and "
              "CfdGradient_TheDeclaredPhaseOrderIsTheCheckOrder, which asserts the order "
              "between the phases a constructible input can reach");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdGradientProblem problem) noexcept;

/// The largest normal-matrix condition number a least-squares neighbourhood may
/// have and still be solved.
///
/// A FLOATING-POINT LIMIT, NOT AN ENGINEERING ONE. The normal matrix is
/// `sum_N w_N d_N (x) d_N`, so its condition number is the SQUARE of the
/// underlying fit's: at `1e12` the solve retains about four of its sixteen
/// digits, and below that the direction of the computed gradient stops being
/// determined by the data. A mesh that a solver should refuse is a different and
/// stricter question, reserved to P19-VALID-001 by ADR-063 finding F1, and
/// `CfdGradientSummary::worstConditioning` is what answers it.
inline constexpr double kCfdGradientConditioningLimit = 1.0e12;

/// What a reconstruction measured about itself.
///
/// THE OMISSION IS REPORTED, NOT ASSUMED AWAY, which is the treatment
/// P19-DIFF-001 gave its uncorrected faces. For Green-Gauss the honest statement
/// of its own error is the skewness of the faces it interpolated across; for
/// least squares it is the conditioning of the systems it solved. Each method
/// fills its own field and leaves the other's at zero, so a zero is a statement
/// about the method and not a missing measurement.
struct CfdGradientSummary {
    /// The method that produced the field.
    CfdGradientMethod method = CfdGradientMethod::LeastSquares;
    /// Cells reconstructed. Every cell the topology holds.
    std::size_t cells = 0;

    /// Boundary faces by the policy this variable takes there. The three sum to
    /// the boundary face count, because `Computed` is refused and ADR-067's
    /// table has no fourth value down either column.
    std::size_t fixedValueFaces = 0;
    std::size_t zeroGradientFaces = 0;
    std::size_t symmetryFaces = 0;

    /// GREEN-GAUSS ONLY: the worst `|skewness|` over the internal faces whose
    /// value was interpolated. Zero for least squares, which interpolates
    /// nothing, and zero for a mesh with no internal face.
    ///
    /// This is the measured bound on Green-Gauss's own departure from linear
    /// exactness: at zero skewness the interpolation is exact, and the error
    /// grows with this number.
    double worstSkewness = 0.0;

    /// LEAST SQUARES ONLY: the worst extreme-eigenvalue ratio of the 3x3 normal
    /// matrix over the mesh. Zero for Green-Gauss, which solves nothing.
    ///
    /// Always at least 1 for a solved system, so a zero here means "no system
    /// was solved" and never "perfectly conditioned".
    double worstConditioning = 0.0;

    /// Whether this method on THIS MESH returns exactly zero for a uniform
    /// field whose boundary values agree with it.
    ///
    /// A STATEMENT ABOUT THE OPERATOR AND THE MESH, not about the input that was
    /// just reconstructed, and the qualifier matters: a Dirichlet wall
    /// prescribing a different value than the interior constant describes a
    /// field that is not uniform, whose true gradient is not zero.
    ///
    /// ```text
    /// LeastSquares   true WHEN THERE IS NO SYMMETRY FACE. Every right-hand side
    ///                is then a DIFFERENCE of two equal cell values, exactly
    ///                0.0, so the solve returns exactly zero whatever the normal
    ///                matrix holds
    /// GreenGauss     true when `sum_f S_f` is BIT-ZERO at every cell AND there
    ///                is no Symmetry face, measured rather than assumed
    /// ```
    ///
    /// THE SYMMETRY CLAUSE WAS MISSING AND A REVIEW FOUND IT. A slip wall's row
    /// value is `U_b,i - U_P,i = -n_i (U_P . n)`, which is NOT zero for a
    /// uniform velocity that is not tangential to the wall -- so the flag was
    /// reading `true` on a mesh where a uniform field reconstructs to something
    /// non-zero, and the reason written here was false rather than merely
    /// incomplete. A slip wall reproduces a uniform field only when that field
    /// is tangential to it, which is a condition on the FIELD and not on the
    /// mesh, so the flag reports the conservative answer.
    ///
    /// For Green-Gauss the bit-zero half is an IMPLICATION THAT IS NOT PROVED,
    /// only measured: the operator computes `sum_f fl(phi * S_f)` in
    /// `cellFaces` order, and `sum_f S_f == 0` bit-exactly does not imply
    /// `sum_f fl(k * S_f) == 0` for an arbitrary `k`. It holds on all eleven
    /// fixtures the suite measures it on, and that is the whole of the evidence.
    bool constantModeIsExact = false;

    friend bool operator==(const CfdGradientSummary&, const CfdGradientSummary&) = default;
};

/// One reconstructed cell-centred gradient field.
///
/// ONE TEMPLATE, DISTINCT TYPES, for the reason `CartesianVector` gives for
/// being one: because the quantity is the template argument, a pressure gradient
/// cannot be passed where a velocity gradient is wanted, and the check is at
/// compile time rather than a runtime tag that a caller can ignore.
///
/// THE VALUES ARE STORED IN SI AND TYPED ON READ. The arithmetic stays in the
/// translation unit behind two explicit instantiations, and no engineering API is
/// unitless: the rule is kept by the accessor rather than by the storage, which
/// is the split ADR-068 chose when it put an expanded field in a caller-owned
/// `std::span<double>`.
///
/// There is NO public constructor. The only door is a reconstruction, which
/// means a field that exists has been through the gate (ADR-070 Decision 3).
/// (The export attribute belongs on the explicit instantiations in the
/// translation unit, not on the template definition.)
template <typename Q>
class CfdGradientField {
public:
    /// The gradient quantity of one component.
    using Component = Q;
    /// The gradient as a vector.
    using Vector = CartesianVector<Q>;

    [[nodiscard]] std::size_t cells() const noexcept { return summary_.cells; }
    [[nodiscard]] const CfdGradientSummary& summary() const noexcept { return summary_; }

    /// The gradient at @p cell. An empty optional for a cell this field does not
    /// hold -- total, never undefined behaviour.
    [[nodiscard]] std::optional<Vector> at(CellIndex cell) const noexcept;

    /// The largest `|gradient|` over the mesh, in the component's unit. Zero for
    /// an empty field and for an exactly uniform input.
    [[nodiscard]] Q largestMagnitude() const noexcept;

    friend bool operator==(const CfdGradientField&, const CfdGradientField&) = default;

private:
    CfdGradientField() = default;

    /// The only thing that may build one, defined in the translation unit that
    /// holds the gate.
    friend struct CfdGradientBuilder;

    /// Three per cell, SI, in `CellIndex` order: `3 * cell + axis`.
    std::vector<double> si_{};
    CfdGradientSummary summary_{};
};

/// A pressure gradient field, in `Pa/m`.
using CfdPressureGradientField = CfdGradientField<PressureGradient>;
/// The gradient of ONE velocity component, in `1/s`.
using CfdVelocityGradientField = CfdGradientField<VelocityGradient>;

/// What to reconstruct a pressure gradient from.
struct CfdGradientInput {
    CfdGradientMethod method = CfdGradientMethod::LeastSquares;
    /// The cell pressures, in SI (Pa), one per cell, in `CellIndex` order.
    std::span<const double> cellValue{};
};

/// What to reconstruct one velocity component's gradient from.
///
/// THE WHOLE VELOCITY, NOT ONE COMPONENT'S SLICE, and that is a design choice
/// with a consequence worth stating. A slip wall gives `U_b = U_P - (U_P . n) n`,
/// so the boundary value of component `i` depends on ALL THREE components of the
/// owner cell's velocity: the gradient of one velocity component is not a
/// function of that component's field alone. ADR-066 says the segregated
/// predictor assembles no row coupling `u` to `v`, and that stays true of the
/// MATRIX; it is not true of this boundary value.
///
/// Taking the full component-blocked array therefore REMOVES A REFUSAL BY
/// CONSTRUCTION rather than by skipping a check: there is no state in which a
/// symmetry face is present and the velocity it needs is absent, so no
/// enumerator for it exists (ADR-070 Decision 4).
struct CfdVelocityGradientInput {
    CfdGradientMethod method = CfdGradientMethod::LeastSquares;
    /// Which component's gradient to reconstruct.
    VectorComponent component = VectorComponent::X;
    /// The cell velocities, in SI (m/s), COMPONENT-BLOCKED as ADR-066 lays them
    /// out: `component * cells + cell`, three times the cell count in all.
    std::span<const double> currentVelocity{};
};

/// Reconstructs the gradient of a cell-centred pressure field.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdPressureGradientField>
reconstructCfdPressureGradient(const CfdTopology& topology, const CfdGeometry& geometry,
                               const PreparedCfdBoundary& boundary, const CfdGradientInput& input);

/// Reconstructs the gradient of one component of a cell-centred velocity field.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdVelocityGradientField>
reconstructCfdVelocityGradient(const CfdTopology& topology, const CfdGeometry& geometry,
                               const PreparedCfdBoundary& boundary,
                               const CfdVelocityGradientInput& input);

/// The problem `reconstructCfdPressureGradient` would report, or none if it
/// would succeed. The same checks in the same order.
///
/// RECORDED FOR WHAT IT IS WORTH: this shares one decision path with the
/// reconstruction, so their agreement cannot fail, exactly as P19-DIFF-001's
/// evidence recorded of its own predictor (its limitation L8). It is here
/// because a caller may legitimately ask whether an input is usable without
/// paying for the answer, and because it pins the MESSAGE; what guards against a
/// swapped tag is the per-case assertion of each tag against an independently
/// named expectation, not this function.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdGradientProblem>
cfdPressureGradientProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                           const PreparedCfdBoundary& boundary, const CfdGradientInput& input) noexcept;

/// The problem `reconstructCfdVelocityGradient` would report, or none.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdGradientProblem>
cfdVelocityGradientProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                           const PreparedCfdBoundary& boundary,
                           const CfdVelocityGradientInput& input) noexcept;

} // namespace bettercad::cfd
