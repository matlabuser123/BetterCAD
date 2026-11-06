#pragma once

/// \file
/// The finite-volume diffusion operator for constant viscosity: `div(mu grad U)`
/// assembled as a per-component scalar matrix and source. ADR-069.
///
/// WHAT THIS IS, IN ONE LINE. A function of a topology, its metrics, a prepared
/// boundary and one viscosity, returning a `CfdSparseMatrix`, a source vector
/// and a summary of what it did. It owns no field, holds no state, and solves
/// nothing.
///
/// PER COMPONENT AND SCALAR, which is ADR-066's decision rather than this
/// milestone's: "The segregated momentum predictor assembles no row that couples
/// `u` to `v`." A velocity assembly is three calls, one per `VectorComponent`,
/// and because component X's block is exactly the cell-centred scalar array the
/// same code assembles a scalar field.
///
/// THE ONE THING A READER MUST NOT ASSUME. Without a caller-supplied face
/// gradient this operator assembles the IMPLICIT part only, and on a
/// non-orthogonal mesh that is not a complete Laplacian. ADR-063 Finding F1
/// measured P16's reference meshes at 31 to 66 degrees mean non-orthogonality
/// with maxima near 90, and said in as many words that "a non-orthogonal
/// correction is mandatory in the discretisation, not an option". So the
/// omission is REPORTED -- `CfdDiffusionSummary::correctionApplied` and
/// `deferredFraction` -- and never silently absorbed. `P19-GRAD-001` is the
/// milestone that supplies the gradient; this header's interface is shaped so
/// that it plugs in without changing.
///
/// THE DECOMPOSITION IS OVER-RELAXED (ADR-069 Decision 2):
///
/// ```text
/// Gamma_f = mu * faceArea(f) / orthogonalDistance(f)
/// E_f     = (|S_f| / cos(theta)) d_hat          the implicit arm
/// T_f     = S_f - E_f                           the deferred arm, T_f . S_f = 0
/// |T_f| / |E_f| = sin(theta)                    bounded by one at every angle
/// ```
///
/// `orthogonalDistance` is `d . n` and ADR-065 already describes it as "the
/// distance a diffusion coefficient divides by", so the geometry layer
/// anticipated this choice.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

/// The gradient of one velocity component, in `1/s`.
///
/// NO NEW QUANTITY IS ADDED TO `core/units` FOR THIS. `Quantity` is templated on
/// a `Dimension` with compile-time multiplication and division, so dividing a
/// `Velocity` by a `Length` already yields the right type; naming it here costs
/// nothing and adding an alias to layer 0 to serve one consumer is the shape of
/// change ADR-063 Decision 7 refused. It is a real type, not a bare double, so
/// ARCHITECTURE.md's rule against a unitless engineering API is kept.
using VelocityGradient = decltype(Velocity{} / Length{});

/// The gradient of ONE velocity component at a face, resolved onto X, Y and Z.
using VelocityGradient3 = CartesianVector<VelocityGradient>;

/// Why a diffusion operator cannot be assembled.
///
/// Declared in PHASE ORDER, and the order is asserted: an input with two faults
/// reports the earlier one. The last value is UNREACHABLE through the document
/// path and is recorded as such rather than claimed tested -- the treatment
/// P19-BC-001 gave `DegenerateFacet`.
enum class CfdDiffusionProblem : std::uint8_t {
    /// The metrics describe a different mesh generation from the topology.
    /// Checked first because every later phase reads both.
    GeometryDescribesAnotherTopology,
    /// The prepared boundary was built against a different generation: its
    /// stamp, its cell count or its boundary-face count disagrees.
    BoundaryDescribesAnotherTopology,
    /// The viscosity is not finite, or is not strictly positive. No `abs`, no
    /// `max(mu, eps)`: a negative viscosity would produce a negative-definite
    /// matrix with no other symptom, which is P18's recorded reason for
    /// refusing a negative conductivity rather than repairing it.
    ViscosityNotUsable,
    /// A `Symmetry` face is present and no current velocity was supplied. Its
    /// cross terms are a real part of the flux, so dropping them would admit a
    /// tangential error at every slip wall -- ADR-067's own reason for making
    /// `Symmetry` a fourth policy rather than folding it onto two.
    SymmetryNeedsTheCurrentVelocity,
    /// A current velocity was supplied whose length is not `3 * cells`, the
    /// component-blocked length ADR-066 fixes.
    CurrentVelocityLengthWrong,
    /// A supplied current velocity holds a value that is not finite.
    ///
    /// CHECKED FOR THE SAME REASON THE VISCOSITY IS. A review found that the
    /// two caller-supplied arrays were validated for LENGTH and never for
    /// VALUE, so a single NaN flowed through the cross terms into the source
    /// while the matrix stayed provably finite -- which made
    /// `CfdSparseMatrix`'s "possession is the evidence" guarantee look like it
    /// covered the whole system when it covers only the coefficients. Refused
    /// here rather than repaired, and the source is never silently cleaned.
    CurrentVelocityNotFinite,
    /// A face gradient was supplied whose length is not `faces`.
    CorrectionLengthWrong,
    /// A supplied face gradient holds a component that is not finite. The
    /// companion of `CurrentVelocityNotFinite`, for the other array.
    CorrectionNotFinite,
    /// A boundary face's velocity policy is `Computed`. UNREACHABLE for `U`:
    /// read down the `U` column of ADR-067's frozen table and the only values
    /// are `FixedValue`, `ZeroGradient` and `Symmetry`; `Computed` appears only
    /// in the `phi` column, at the two pressure boundaries.
    VelocityPolicyIsAnOutcome,
};

/// The nine, in phase order.
///
/// THERE IS NO `AssembledMatrixInconsistent`, AND THAT IS A CORRECTION. An
/// earlier draft declared one, documented as the tag a refusal from
/// `CfdSparseMatrix::fromCsr` would carry -- and nothing ever emitted it: the
/// assembly returns a plain `ErrorCode::Internal` with the sparse problem's
/// name in its message, because `Result<CfdDiffusionSystem>` has no channel for
/// a tag. A review pointed out that a tag no path produces and no test can read
/// is a claim rather than a guarantee, in the milestone that records
/// P19-BC-001's lesson about exactly that. So it is removed, and the assembly's
/// internal inconsistencies are UNTAGGED `Internal` errors by design: they are
/// unreachable through every public path, since the symbolic pass reserves
/// every pair the numeric pass can ask for.
inline constexpr std::array<CfdDiffusionProblem, 9> kCfdDiffusionProblems{
    CfdDiffusionProblem::GeometryDescribesAnotherTopology,
    CfdDiffusionProblem::BoundaryDescribesAnotherTopology,
    CfdDiffusionProblem::ViscosityNotUsable,
    CfdDiffusionProblem::SymmetryNeedsTheCurrentVelocity,
    CfdDiffusionProblem::CurrentVelocityLengthWrong,
    CfdDiffusionProblem::CurrentVelocityNotFinite,
    CfdDiffusionProblem::CorrectionLengthWrong,
    CfdDiffusionProblem::CorrectionNotFinite,
    CfdDiffusionProblem::VelocityPolicyIsAnOutcome};

static_assert(kCfdDiffusionProblems.size() == 9,
              "every CfdDiffusionProblem belongs in kCfdDiffusionProblems, and a tenth "
              "enumerator left out of the array would otherwise compile: -Wswitch guards "
              "toString, not this array");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdDiffusionProblem problem) noexcept;

/// A refusal with its tag.
struct CfdDiffusionFailure {
    CfdDiffusionProblem problem{};
    Error error{};
};

/// The over-relaxed decomposition of one face's area vector.
///
/// EXPOSED BECAUSE THE CORRECTION IS CALLER-SUPPLIED. A caller forming
/// `mu (grad U)_f . T_f` needs `T_f`, and computing it again outside would be a
/// second copy of the arithmetic that could disagree with the assembly's.
struct CfdFaceDecomposition {
    /// `E_f`, parallel to `d`. What the matrix carries.
    AreaVector3D implicitArm{};
    /// `T_f = S_f - E_f`, orthogonal to `S_f`. What a gradient must multiply.
    AreaVector3D deferredArm{};
    /// `|T_f| / |E_f|`, which is `sin(theta)` for this decomposition: the
    /// fraction of the flux the implicit arm does not carry.
    double deferredFraction = 0.0;

    friend bool operator==(const CfdFaceDecomposition&, const CfdFaceDecomposition&) = default;
};

/// The linear-interpolation weight of @p face: `|C_N - C_f| / |C_N - C_P|`, the
/// share the OWNER's value carries when a face value is interpolated between
/// the two cells (ADR-069 Decision 7).
///
/// One for a boundary face, which has no neighbour to interpolate with, and
/// none for a face @p geometry does not hold.
///
/// NAMED AND PUBLIC BECAUSE THE INTERPOLATION HAD TO BE MORE THAN PROSE.
/// ADR-069 Decision 7 said the weight was "named and implemented now, with the
/// single call site a variable viscosity will enter through" -- and both reviews
/// found that nothing of the kind existed. For a constant viscosity
/// `mu_f = w mu_P + (1 - w) mu_N` is `mu` for every weight, so the interpolation
/// is the identity and a test can only assert that it IS the identity over the
/// range of weights a real mesh presents. That is what this accessor makes
/// possible, and it is the one function a variable viscosity changes.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<double>
cfdFaceInterpolationWeight(const CfdGeometry& geometry, FaceIndex face) noexcept;

/// The viscosity AT @p face, interpolated by `cfdFaceInterpolationWeight`.
///
/// THE SINGLE CALL SITE. For the constant viscosity P19 v1 is limited to
/// (ADR-058 excludes both sources of a variable one) this returns @p viscosity
/// for every weight; it exists so that a variable viscosity is a change to one
/// function rather than a search through the assembly.
[[nodiscard]] BETTERCAD_CFD_EXPORT DynamicViscosity
cfdFaceViscosity(const CfdGeometry& geometry, FaceIndex face, DynamicViscosity viscosity) noexcept;

/// The decomposition at @p face, or none for a face @p geometry does not hold.
///
/// `d` is the owner-to-neighbour vector on an internal face and the
/// cell-to-face vector on a boundary face, exactly as `orthogonalDistance`
/// defines it, so one function serves both.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdFaceDecomposition>
cfdFaceDecomposition(const CfdGeometry& geometry, FaceIndex face) noexcept;

/// What one assembly did, so a consumer need not re-derive it and an evidence
/// file need not guess.
struct CfdDiffusionSummary {
    std::size_t cells = 0;
    std::size_t internalFaces = 0;
    std::size_t fixedValueFaces = 0;
    std::size_t zeroGradientFaces = 0;
    std::size_t symmetryFaces = 0;
    /// True exactly when a face gradient was supplied, so the deferred arm was
    /// applied. FALSE MEANS THE ASSEMBLED OPERATOR IS EXACT ONLY ON AN
    /// ORTHOGONAL MESH.
    bool correctionApplied = false;
    /// The largest `|T_f| / |E_f|` over the faces that CONTRIBUTE a
    /// coefficient, which is `sin(theta)` of the worst such face. Zero on a
    /// perfectly orthogonal mesh, and zero when no face contributes.
    ///
    /// CONTRIBUTING, NOT ALL, AND THE DISTINCTION IS A CORRECTION. An earlier
    /// draft maxed over every face including `ZeroGradient` ones, whose
    /// diffusive flux is zero and which therefore defer nothing -- so the
    /// number overstated what had been omitted, while this comment already said
    /// "assembled". A review found the two did not agree. An internal face and
    /// a `FixedValue` or `Symmetry` boundary face contribute; a `ZeroGradient`
    /// face does not, and no longer raises this.
    double deferredFraction = 0.0;
    /// True exactly when the boundary added NOTHING to any diagonal, which is
    /// exactly when the matrix is SINGULAR with the constant vector in its null
    /// space. Physically correct, and named so that a consumer is not surprised
    /// by it.
    ///
    /// SAID THAT WAY ROUND DELIBERATELY. This comment first read "when no
    /// boundary face prescribes a value", which is false in the `Symmetry`
    /// direction: a slip wall prescribes no value yet adds `Gamma n_i^2` to the
    /// diagonal, and so lifts the mode whenever `n_i` is not zero -- while a
    /// slip wall whose normal IS perpendicular to this component's axis adds
    /// exactly zero and leaves it. The flag is measured from what was added,
    /// because that cannot disagree with the matrix.
    bool constantModeIsInTheNullSpace = false;

    friend bool operator==(const CfdDiffusionSummary&, const CfdDiffusionSummary&) = default;
};

/// One assembled component system: `A u = b`.
struct CfdDiffusionSystem {
    /// `A`, approximating `-div(mu grad)` so the diagonal is positive. Square,
    /// `cells` by `cells`, symmetric to the last bit.
    CfdSparseMatrix matrix;
    /// `b`, in SI, one entry per cell. Holds the prescribed-value
    /// contributions, a `Symmetry` face's cross terms and -- when supplied --
    /// the deferred correction.
    std::vector<double> source;
    CfdDiffusionSummary summary;
};

/// Everything the assembly needs that is not geometry.
struct CfdDiffusionInput {
    /// Constant over the domain. ADR-058 excludes both sources of a variable
    /// viscosity from P19 v1, and ADR-069 Decision 7 names the single call site
    /// one would enter through.
    DynamicViscosity viscosity{};
    /// Which component's system this is.
    VectorComponent component = VectorComponent::X;
    /// The current cell velocities of EVERY component, component-blocked per
    /// ADR-066 (`comp * cells + cell`), so `3 * cells` entries.
    ///
    /// REQUIRED ONLY WHEN A `Symmetry` FACE IS PRESENT, and then only for its
    /// cross terms. An assembly without a slip wall is a pure function of the
    /// geometry, the viscosity and the boundary values; one with a slip wall is
    /// not, and that asymmetry is ADR-069 Decision 6's and is stated rather
    /// than hidden.
    std::span<const double> currentVelocity{};
    /// The gradient of THIS component at each face, one entry per face, in
    /// `FaceIndex` order. Empty means the correction is omitted and reported.
    std::span<const VelocityGradient3> faceGradient{};
};

/// Assembles the diffusion operator for one component.
///
/// THE LOOP IS OVER FACES, ONCE EACH, which is what makes conservation
/// structural rather than checked: an internal face writes `+Gamma` to both
/// diagonals and `-Gamma` to both off-diagonals in one visit, so there is no
/// second visit that could disagree. A loop over cells and their faces would
/// see each internal face twice and have to halve, skip or compare.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdDiffusionSystem>
assembleCfdDiffusion(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary, const CfdDiffusionInput& input);

/// The refusal `assembleCfdDiffusion` would give, with its tag, or none if it
/// would succeed.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdDiffusionFailure>
cfdDiffusionFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                    const PreparedCfdBoundary& boundary, const CfdDiffusionInput& input);

/// The problem alone, allocation-free.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdDiffusionProblem>
cfdDiffusionProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                    const PreparedCfdBoundary& boundary, const CfdDiffusionInput& input) noexcept;

/// The relative symmetry bound `assembleCfdDiffusion` requires of its own
/// matrix.
///
/// A CORRECT ASSEMBLY IS SYMMETRIC TO THE LAST BIT, so this bound is not tight
/// to anything: `Gamma_f` read from the owner and from the neighbour is the SAME
/// double, because `d' . n' = (-d) . (-n) = d . n` with both negations exact in
/// IEEE-754 and `faceArea` one stored number. The gate exists so that a
/// defective scatter is a refusal and not a silent asymmetry -- thermal's
/// recorded reason for the same constant, at the same value.
inline constexpr double kCfdDiffusionSymmetryTolerance = 1.0e-13;

} // namespace bettercad::cfd
