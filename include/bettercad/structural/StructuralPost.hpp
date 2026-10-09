#pragma once

// P17-POST-001 -- displacement, strain, stress and the derived invariants.
//
// IT RECOVERS, IT DOES NOT RE-DERIVE. The two formulas at the centre of this
// milestone already exist and are already qualified:
//
//   eps   = B u_e      Tet4Kinematics::strainFrom       P17-ELEM-001
//   sigma = D eps      ElasticityMatrix::stressFrom     P17-ELEM-001
//
// and both are public for exactly this reason -- Tet4Element.hpp says so by
// name. There is no second B and no second D in this translation unit, no
// shape-function gradient, no `lambda =`, no `E * nu`, and no reimplementation
// of the engineering-shear convention. What is new here is the tensor mapping,
// the principal values, von Mises, and the traversal that visits every node
// and every element exactly once in the mesh's own order.
//
// THE RESULT IS A STAGE PRODUCT (ADR-040). `RecoveredFields` carries the
// `AssemblySource` of the system it recovered from -- the same six fields the
// solve carried -- and answers no currentness question of its own. It is
// deliberately NOT a `StructuralResult`: that type needs a reaction channel
// this milestone may not compute and an analysis identity recovery cannot see,
// and filling either by hand is how a stale result acquires a current-looking
// stamp.
//
// ELEMENT-CONSTANT, AND THAT IS THE PHYSICS. A Tet4's shape functions are
// linear, so `B` is constant over the element and so are strain and stress.
// There is one `Strain6` and one `Stress6` per tetrahedron. No nodal stress,
// no smoothing, no averaging and no extrapolation: those carry extra semantics
// and belong to a milestone that is authorized for them.
//
// WHAT THIS FILE DOES NOT DO. No solve -- it consumes one. No reactions:
// `SolvedSystem::fullResidual()` retains them and `P17-REACTION-001` owns
// their semantics. No acceptance policy: nothing here decides whether a stress
// is safe, and no mesh-quality threshold is re-asked. No display units: SI in,
// SI out. No mesh mutation and no deformed geometry.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralData.hpp>
#include <bettercad/structural/StructuralMaterial.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>
#include <bettercad/structural/Tet4Element.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// Displacement magnitude
// ---------------------------------------------------------------------------

/// `|u| = sqrt(ux^2 + uy^2 + uz^2)`, in metres.
///
/// `std::hypot`, WHICH IS WHAT THE REST OF THE TREE USES. `Point3D`'s own
/// `distance` is `std::hypot((b.x - a.x).si(), ...)`, so this is the same norm
/// the geometry layer already trusts rather than a second one. The three-
/// argument overload is correctly rounded and does not overflow for an
/// intermediate square, which a hand-written `sqrt(x*x + y*y + z*z)` does.
///
/// NOT `abs(ux + uy + uz)` and not a component maximum. Said out loud because
/// both are plausible-looking and both are wrong, and for an axis-aligned
/// displacement all three agree -- which is why the tests use a non-axis-
/// aligned case.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Length
displacementMagnitude(const Translation3D& displacement) noexcept;

// ---------------------------------------------------------------------------
// Voigt to tensor
// ---------------------------------------------------------------------------

/// The symmetric 3x3 stress tensor, in Pa.
///
/// SIX FIELDS, NINE POSITIONS, AND SYMMETRY IS STRUCTURAL. There is one `xy`,
/// so `at(0, 1)` and `at(1, 0)` return the same number and an asymmetric
/// stress tensor cannot be built. The symmetry test is therefore a test of
/// `at`, which is the thing that can be wrong.
///
/// THE MAPPING IS THE CONTRACT, and the sixth component is where it is got
/// wrong (ADR-040):
///
/// ```text
///     Voigt 3  XY  ->  at(0,1)  at(1,0)
///     Voigt 4  YZ  ->  at(1,2)  at(2,1)
///     Voigt 5  ZX  ->  at(2,0)  at(0,2)
/// ```
///
/// `zx` and `xz` are the same number, so a transposed sixth component is
/// numerically invisible. What it breaks is any code that indexes the tensor
/// by hand -- which is why nothing does, and everything goes through `at`.
///
/// NO EXPORT MACRO. Every member is defined here and `constexpr`, so the macro
/// would expand to `dllimport` on an inline function and fail only in
/// debug-shared-ext.
struct StressTensor3 {
    Stress xx{};
    Stress yy{};
    Stress zz{};
    Stress xy{};
    Stress yz{};
    Stress zx{};

    /// `S(row, column)`. Out of range returns zero, the convention
    /// `Tet4Kinematics::b` and `ElasticityMatrix::d` already set.
    [[nodiscard]] constexpr Stress at(std::size_t row, std::size_t column) const noexcept {
        if (row > 2 || column > 2) {
            return Stress{};
        }
        if (row == column) {
            return row == 0 ? xx : (row == 1 ? yy : zz);
        }
        const std::size_t lower = row < column ? row : column;
        const std::size_t upper = row < column ? column : row;
        if (lower == 0 && upper == 1) {
            return xy;
        }
        if (lower == 1 && upper == 2) {
            return yz;
        }
        return zx; // (0, 2)
    }

    /// `tr(S) = sxx + syy + szz`, the first invariant.
    [[nodiscard]] constexpr Stress trace() const noexcept { return xx + yy + zz; }

    friend constexpr bool operator==(const StressTensor3&, const StressTensor3&) noexcept = default;
};

/// The symmetric small-strain tensor, dimensionless.
///
/// THE OFF-DIAGONALS ARE `gamma / 2`, AND THAT IS THE WHOLE POINT OF THE TYPE.
/// `Strain6` stores ENGINEERING shear, so
///
/// ```text
///     eps_xy = gammaXy / 2
/// ```
///
/// and putting a full `gammaXy` into `at(0, 1)` doubles every shear term of
/// every principal strain. The fields here are named `xy`, `yz`, `zx` rather
/// than `gammaXy` for exactly that reason: a `StrainTensor3` holds TENSOR
/// shear and a `Strain6` holds engineering shear, and the two names must not
/// look alike.
struct StrainTensor3 {
    double xx = 0.0;
    double yy = 0.0;
    double zz = 0.0;
    /// Tensor shear, `gammaXy / 2`.
    double xy = 0.0;
    double yz = 0.0;
    double zx = 0.0;

    [[nodiscard]] constexpr double at(std::size_t row, std::size_t column) const noexcept {
        if (row > 2 || column > 2) {
            return 0.0;
        }
        if (row == column) {
            return row == 0 ? xx : (row == 1 ? yy : zz);
        }
        const std::size_t lower = row < column ? row : column;
        const std::size_t upper = row < column ? column : row;
        if (lower == 0 && upper == 1) {
            return xy;
        }
        if (lower == 1 && upper == 2) {
            return yz;
        }
        return zx;
    }

    /// `tr(E) = exx + eyy + ezz`, the volumetric strain.
    [[nodiscard]] constexpr double trace() const noexcept { return xx + yy + zz; }

    friend constexpr bool operator==(const StrainTensor3&, const StrainTensor3&) noexcept = default;
};

/// `Stress6 -> StressTensor3`. A relabelling: no factor anywhere.
[[nodiscard]] constexpr StressTensor3 tensorOf(const Stress6& stress) noexcept {
    return StressTensor3{.xx = stress.xx,
                         .yy = stress.yy,
                         .zz = stress.zz,
                         .xy = stress.xy,
                         .yz = stress.yz,
                         .zx = stress.zx};
}

/// `Strain6 -> StrainTensor3`, halving each engineering shear.
[[nodiscard]] constexpr StrainTensor3 tensorOf(const Strain6& strain) noexcept {
    return StrainTensor3{.xx = strain.xx,
                         .yy = strain.yy,
                         .zz = strain.zz,
                         .xy = 0.5 * strain.gammaXy,
                         .yz = 0.5 * strain.gammaYz,
                         .zx = 0.5 * strain.gammaZx};
}

// ---------------------------------------------------------------------------
// Invariants
// ---------------------------------------------------------------------------

/// `sigma_h = tr(S) / 3`, in Pa.
///
/// THE NORMAL-STRESS SIGN CONVENTION, UNCHANGED (ADR-040). A uniform
/// compressive state of `-100 MPa` has `sigma_h = -100 MPa`, not `+100 MPa`.
/// It is named `hydrostaticStress` and not `pressure` for that reason: a mean
/// pressure is `-sigma_h` under the usual mechanics convention, it is a
/// different quantity, and it is deliberately not defined here so the two
/// cannot be conflated.
[[nodiscard]] constexpr Stress hydrostaticStress(const Stress6& stress) noexcept {
    return (stress.xx + stress.yy + stress.zz) / 3.0;
}

/// The von Mises equivalent stress of the FULL 3D state, in Pa.
///
/// ```text
///     sigma_vm = sqrt( 0.5 ( (sxx-syy)^2 + (syy-szz)^2 + (szz-sxx)^2 )
///                      + 3 ( txy^2 + tyz^2 + tzx^2 ) )
/// ```
///
/// ALL SIX COMPONENTS, AND NO PLANE-STRESS SIMPLIFICATION. A 2D formula drops
/// `szz`, `tyz` and `tzx` and agrees with this one on every uniaxial and every
/// pure-XY-shear case, which is why the qualification fixture has all three
/// nonzero and why a mutation probe replaces this body with the plane-stress
/// form.
///
/// NON-NEGATIVE BY CONSTRUCTION. The radicand is a sum of squares with
/// positive coefficients, so it cannot be negative for finite input and there
/// is no `abs` and no clamp -- a repaired radicand would hide a formula error
/// rather than a rounding one.
///
/// INDEPENDENT OF HYDROSTATIC STRESS. Adding `q I` to the state leaves every
/// difference `sxx - syy` unchanged and touches no shear term, so `sigma_vm`
/// is unchanged. That is a property of the formula rather than a claim about
/// it, and it is measured.
///
/// `noexcept` AND TOTAL, with no `Result`. For finite input the answer is
/// finite and non-negative; for non-finite input it propagates, and the
/// finiteness gate belongs upstream where the ROOT CAUSE is still visible --
/// `recoverFields` refuses a non-finite stress before it ever reaches here, so
/// the diagnostic says "non-finite stress" rather than "von Mises failed".
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Stress vonMisesStress(const Stress6& stress) noexcept;

/// The three principal stresses, in Pa, **descending**.
///
/// ```text
///     sigma1 >= sigma2 >= sigma3
/// ```
///
/// BETTERCAD'S ORDER, NOT THE LIBRARY'S. The eigensolver's output order is not
/// part of any contract and is not relied on: the three values are sorted
/// after the solve. A repeated eigenvalue is an equality and not a special
/// case, so a hydrostatic state gives three equal values and an axisymmetric
/// one gives two.
///
/// No eigenvectors. They are not needed for a magnitude, their sign is
/// arbitrary, and computing them would add an output nothing can verify.
struct PrincipalStresses {
    Stress sigma1{};
    Stress sigma2{};
    Stress sigma3{};

    /// By index: 0 -> sigma1, 1 -> sigma2, 2 -> sigma3. Out of range is zero.
    [[nodiscard]] constexpr Stress at(std::size_t which) const noexcept {
        return which == 0 ? sigma1 : (which == 1 ? sigma2 : (which == 2 ? sigma3 : Stress{}));
    }

    /// `sigma1 + sigma2 + sigma3`, which equals `tr(S)`.
    [[nodiscard]] constexpr Stress sum() const noexcept { return sigma1 + sigma2 + sigma3; }

    friend constexpr bool operator==(const PrincipalStresses&,
                                     const PrincipalStresses&) noexcept = default;
};

/// The three principal strains, dimensionless, descending.
///
/// Computed from `StrainTensor3`, so the `gamma / 2` has already been applied.
/// A pure engineering shear `gammaXy = gamma` gives `+|gamma|/2, 0, -|gamma|/2`
/// -- and `+|gamma|, 0, -|gamma|` if the halving were missed, which is the test
/// that measures it.
struct PrincipalStrains {
    double e1 = 0.0;
    double e2 = 0.0;
    double e3 = 0.0;

    [[nodiscard]] constexpr double at(std::size_t which) const noexcept {
        return which == 0 ? e1 : (which == 1 ? e2 : (which == 2 ? e3 : 0.0));
    }

    [[nodiscard]] constexpr double sum() const noexcept { return e1 + e2 + e3; }

    friend constexpr bool operator==(const PrincipalStrains&,
                                     const PrincipalStrains&) noexcept = default;
};

/// Principal stresses of @p stress, or a failure if the eigensolve did not
/// converge or produced a non-finite value.
///
/// A `Result` rather than a total function, because an eigensolve is the one
/// step here that can fail. There is no partial answer: either three finite
/// sorted values or a diagnostic.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<PrincipalStresses>
principalStresses(const Stress6& stress);

/// Principal strains of @p strain, through `StrainTensor3`.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<PrincipalStrains>
principalStrains(const Strain6& strain);

// ---------------------------------------------------------------------------
// Recovered fields
// ---------------------------------------------------------------------------

/// One node's recovered displacement.
///
/// IT CARRIES ITS `NodeId`, which `StructuralResult`'s dense channel does not.
/// The reason is that it makes the cardinality and uniqueness contract
/// checkable from the result alone, without a mesh in hand: "every current
/// node exactly once, ascending" is then a property of the vector rather than
/// an assumption about how it was built. Four bytes per node, and storage
/// stays `O(nodes)`.
struct NodalDisplacement {
    meshing::NodeId node{};
    Translation3D displacement{};
    /// `|u|`, in metres. Stored rather than recomputed so that a consumer
    /// cannot compute it a second, different way.
    Length magnitude{};

    friend constexpr bool operator==(const NodalDisplacement&,
                                     const NodalDisplacement&) noexcept = default;
};

/// One tetrahedron's recovered state: constant over the element.
struct ElementFields {
    meshing::ElementId element{};
    /// `eps = B u_e`, dimensionless, engineering shear, in `TensorComponent`
    /// order.
    Strain6 strain{};
    /// `sigma = D eps`, in Pa, same order.
    Stress6 stress{};
    PrincipalStresses principalStress{};
    PrincipalStrains principalStrain{};
    /// Full-3D von Mises, in Pa.
    Stress vonMises{};
    /// `tr(S)/3`, in Pa, normal-stress sign convention.
    Stress hydrostatic{};

    friend constexpr bool operator==(const ElementFields&,
                                     const ElementFields&) noexcept = default;
};

/// Why fields cannot be recovered.
///
/// WHICH VALUES ARE REACHABLE FROM WHERE, because the honest answer is not
/// "all of them from everywhere" and a refusal nobody can test is a refusal
/// nobody has checked.
///
/// Possession of a `StructuralModel` proves the mesh passed P16's validation
/// and the material passed P15's (ADR-036), and possession of a `SolvedSystem`
/// proves every displacement is finite (ADR-039). So `recoverFields` can only
/// be made to fail by a MISMATCH -- which is exactly the class of defect it
/// exists to prevent -- and the per-element refusals need the kernel:
///
/// ```text
/// through recoverFields      SolutionSourceMismatch, MeshMismatch,
///                            MaterialSourceMismatch
/// through recoverElementFields
///                            NonFiniteDisplacement, ElementRejected,
///                            NonFiniteStrain, NonFiniteStress. It is the
///                            public numerical kernel for this reason, as
///                            `solveSymmetricSparse` is P17-SOLVE's
/// not reachable by ANY input MeshHasNoElements, ElementNodeMissing,
///                            DegreeOfFreedomMissing, InvalidMaterial,
///                            PrincipalValueFailure, NonFiniteDerivedResult
/// ```
///
/// The last six need a mesh, a material or an arithmetic outcome that cannot
/// occur. The first four are closed by the validated types; the last two are
/// closed by the check BEFORE them -- a finite symmetric 3x3 has finite
/// eigenvalues and `SelfAdjointEigenSolver` cannot fail on one, and von Mises
/// and the mean are finite whenever the stress is. Two mutation probes record
/// that honestly by SURVIVING (M14, M15), rather than being dressed up as
/// kills.
///
/// They are kept because the refusal must be structural rather than a
/// comment: a later milestone building a model from a file, a script or a
/// network message opens the first four, and deleting the last two would mean
/// ignoring the one failure signal Eigen offers -- the mistake ADR-039 was
/// written against, inverted. A silently skipped element would publish a
/// softer body with nothing reporting it.
enum class RecoveryProblem : std::uint8_t {
    /// The solved system was not produced from the system handed in. A
    /// field-complete `AssemblySource` comparison, so an edit to any of the
    /// six dependencies is caught.
    SolutionSourceMismatch,
    /// The displacement field is not this mesh's: a different stamp, or the
    /// right stamp and the wrong node count.
    MeshMismatch,
    /// The material is not the one the system was assembled with -- a
    /// different `MaterialId`, or the same one at a different revision.
    /// REFUSED RATHER THAN USED: recomputing stress from an old displacement
    /// and a new modulus produces a number that is wrong in a way nothing
    /// downstream can detect.
    MaterialSourceMismatch,
    /// The mesh carries no tetrahedron, so there is no element field to
    /// recover. The same classification `AssemblyProblem` gives it.
    MeshHasNoElements,
    /// An element names a node the mesh does not have.
    ElementNodeMissing,
    /// A degree of freedom of an element has no index in the numbering.
    DegreeOfFreedomMissing,
    /// `P17-ELEM-001` refused the element geometry: degenerate, inverted or
    /// non-finite. PROPAGATED THROUGH THE SHARED KINEMATICS, never re-derived
    /// here -- there is no second determinant test in this module.
    ElementRejected,
    /// `isotropicElasticity` refused the elastic constants.
    InvalidMaterial,
    /// A recovered nodal displacement is not finite. Checked FIRST, so a NaN
    /// in the solution is reported as a displacement problem rather than as a
    /// downstream eigensolver failure.
    NonFiniteDisplacement,
    /// A recovered strain is not finite.
    NonFiniteStrain,
    /// A recovered stress is not finite.
    NonFiniteStress,
    /// An eigensolve did not converge, or a principal value came out
    /// non-finite from finite input.
    PrincipalValueFailure,
    /// A derived scalar -- von Mises, hydrostatic stress or a displacement
    /// magnitude -- came out non-finite although its input was finite.
    NonFiniteDerivedResult,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(RecoveryProblem problem) noexcept;

/// Every recovered field of one solved analysis on one mesh.
///
/// POSSESSION IS THE EVIDENCE (ADR-036). There is no public constructor and
/// exactly one friend, so holding a `RecoveredFields` proves that the
/// solution, the mesh, the numbering and the material all agreed, that every
/// node and every element is present exactly once, and that every value --
/// displacement, strain, stress, every principal value, von Mises and
/// hydrostatic stress -- is finite. A partially recovered result is
/// unrepresentable rather than discouraged.
///
/// ATOMIC. The vectors are built in a local and moved in only after the last
/// check passes, so an element that fails at index 927 publishes nothing
/// rather than 926 valid entries and a claim.
///
/// ORDERED BY THE MESH, in the mesh's own enumeration: nodes ascending by
/// `NodeId`, elements ascending by `ElementId`, both guaranteed by P16 and
/// neither read from an unordered container. The same mesh and solution give
/// the same sequence in every preset and on every run.
///
/// `O(nodes + elements)`. The mesh is not copied in -- only its stamp -- and
/// neither is `K`. There is no element-by-node relationship anywhere.
class BETTERCAD_STRUCTURAL_EXPORT RecoveredFields {
public:
    /// What the system these fields came from was assembled from. COPIED
    /// FORWARD from the solved system, never derived: there is no way to reach
    /// this and rewrite it, which is what stops a stale result being made to
    /// look current.
    [[nodiscard]] const AssemblySource& source() const noexcept { return source_; }

    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }

    /// Whether these fields belong to @p mesh.
    ///
    /// Stamp AND counts. Two meshes of the same body can have identical node
    /// and element counts and be different meshes, so the stamp is what
    /// decides; the counts close the case `MeshDofMap::describes` records,
    /// where a builder grows a mesh without reissuing its stamp.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(mesh_) && mesh.nodes().size() == displacements_.size() &&
               mesh.tetrahedra().size() == elements_.size();
    }

    /// One entry per current node, ascending by `NodeId`.
    [[nodiscard]] std::span<const NodalDisplacement> displacements() const noexcept {
        return displacements_;
    }

    /// One entry per current tetrahedron, ascending by `ElementId`.
    [[nodiscard]] std::span<const ElementFields> elements() const noexcept { return elements_; }

    /// The recovered displacement of @p node of @p mesh.
    ///
    /// IT TAKES THE MESH, AND REFUSES IF IT IS NOT THIS ONE. That is the
    /// contract `StructuralResult::displacementOf` already has -- "the refusal
    /// ADR-031 requires" -- and without it a caller holding fields recovered
    /// on mesh M1 could ask for `NodeId(1)` while thinking of M2 and get a
    /// plausible number, because P16 restarts its handles at 1 after every
    /// remesh. `describes(mesh)` is asked FIRST, so the stale lookup is
    /// impossible rather than documented.
    ///
    /// Binary search over the ascending handles, which is the access model
    /// `Mesh`, `MeshDofMap` and `StructuralResult` all use. Provided so a
    /// caller never computes a position itself, which is where an assumption
    /// about dense ids would otherwise appear.
    [[nodiscard]] Result<NodalDisplacement> displacementOf(const meshing::Mesh& mesh,
                                                           meshing::NodeId node) const;

    /// The recovered fields of @p element of @p mesh, with the same refusal.
    [[nodiscard]] Result<ElementFields> fieldsOf(const meshing::Mesh& mesh,
                                                 meshing::ElementId element) const;

    /// The largest `|u|` over every node, in metres. Non-negative and finite.
    ///
    /// Taken in node order, so it is reproducible: a maximum over an unordered
    /// container would be too, but the index of the node attaining it would
    /// not, and a report naming "the most displaced node" has to be stable.
    [[nodiscard]] Length largestDisplacementMagnitude() const noexcept {
        return largestDisplacement_;
    }

    /// The largest von Mises stress over every element, in Pa.
    [[nodiscard]] Stress largestVonMises() const noexcept { return largestVonMises_; }

    friend bool operator==(const RecoveredFields&, const RecoveredFields&) = default;

private:
    /// THE ONLY CONSTRUCTOR, AND THERE IS NO DEFAULT ONE -- the shape
    /// `PreparedRestraints`, `GlobalStructuralSystem` and `SolvedSystem` use,
    /// for the reason ADR-036 gives.
    RecoveredFields(AssemblySource source, meshing::MeshStamp mesh,
                    Length largestDisplacement, Stress largestVonMises,
                    std::vector<NodalDisplacement> displacements,
                    std::vector<ElementFields> elements)
        : source_(std::move(source)), mesh_(std::move(mesh)),
          largestDisplacement_(largestDisplacement), largestVonMises_(largestVonMises),
          displacements_(std::move(displacements)), elements_(std::move(elements)) {}

    friend BETTERCAD_STRUCTURAL_EXPORT Result<RecoveredFields>
    recoverFields(const StructuralModel& model, const StructuralMaterial& material,
                  const GlobalStructuralSystem& system, const SolvedSystem& solution);

    AssemblySource source_;
    meshing::MeshStamp mesh_;
    Length largestDisplacement_;
    Stress largestVonMises_;
    std::vector<NodalDisplacement> displacements_;
    std::vector<ElementFields> elements_;
};

/// Recovers every field of @p solution over @p model's mesh.
///
/// FOUR INPUTS, AND EACH IS CHECKED AGAINST THE OTHERS rather than trusted:
///
/// ```text
/// solution.source() == system.source()     the solution is THIS system's
/// system.numbering().describes(mesh)       the numbering is THIS mesh's
/// solution.describes(mesh)                 the field is THIS mesh's size
/// material matches source.material         and its revision too
/// ```
///
/// which is what makes "u from M1 on mesh M2" and "material B with a
/// displacement solved under material A" refusals rather than plausible
/// numbers. Same-sized meshes with repeating `NodeId`s do not get past the
/// first two: a `MeshStamp` is identity and a count is not.
///
/// NO SOLVE. This consumes a `SolvedSystem` and never produces one; there is
/// no hidden second factorisation and no iteration.
///
/// NO MUTATION. `model`, `material`, `system` and `solution` are all const
/// references and nothing is written through them. In particular no node is
/// moved to a deformed position: a deformed shape is display state and belongs
/// to `P17-VIZ-001`.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<RecoveredFields>
recoverFields(const StructuralModel& model, const StructuralMaterial& material,
               const GlobalStructuralSystem& system, const SolvedSystem& solution);

/// The problem `recoverFields` would report, or none if it would succeed.
///
/// Same checks in the same order, through the same implementation, for a
/// caller that wants to classify or count the reason rather than parse a
/// message. The two cannot disagree because there is one `run`.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<RecoveryProblem>
fieldRecoveryProblem(const StructuralModel& model, const StructuralMaterial& material,
                     const GlobalStructuralSystem& system, const SolvedSystem& solution);

/// The complete recovered state of ONE tetrahedron, from its corner positions
/// and its corner displacements.
///
/// THE PUBLIC NUMERICAL KERNEL OF THIS MILESTONE, and it is public for the
/// reason `solveSymmetricSparse` is. Two things cannot otherwise be reached:
///
/// ```text
/// the ANALYTICAL cases    no solved structural fixture poses an element whose
///                         strain is known in closed form, so the affine field
///                         has to be handed in directly
/// the FINITENESS refusals no VALIDATED mesh can carry a non-finite
///                         displacement and no qualified solve can produce
///                         one, so NonFiniteStrain, NonFiniteStress,
///                         PrincipalValueFailure and NonFiniteDerivedResult are
///                         unreachable from `recoverFields` -- and a refusal
///                         that cannot be tested is a refusal nobody has
///                         checked
/// ```
///
/// `recoverFields` calls THIS once per tetrahedron, so the path the tests
/// exercise is the path production runs; there is no second implementation and
/// no test-only branch. It is not a back door: it takes no document, no mesh
/// and no source stamp, so it cannot be used to publish a result.
///
/// @p displacements are in the element's own node order -- the order the mesh
/// stores them -- NOT reordered by coordinate.
///
/// Fails if the geometry is refused by the shared kinematics, or if any
/// recovered or derived value is not finite. The checks run in the order the
/// `RecoveryProblem` values are declared, so the diagnostic names the ROOT
/// cause rather than the first thing downstream to notice it.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<ElementFields>
recoverElementFields(meshing::ElementId element,
                     const std::array<Point3D, kTet4Nodes>& corners,
                     const std::array<Translation3D, kTet4Nodes>& displacements,
                     const ElasticityMatrix& elasticity);

/// The problem `recoverElementFields` would report, or none if it would
/// succeed. Same checks, same order, same implementation.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<RecoveryProblem>
elementRecoveryProblem(const std::array<Point3D, kTet4Nodes>& corners,
                       const std::array<Translation3D, kTet4Nodes>& displacements,
                       const ElasticityMatrix& elasticity);

/// The twelve-component local displacement vector of one tetrahedron, in
/// `localDofIndex` order, gathered from a solved field.
///
/// EXPOSED SO THE ORDERING CAN BE TESTED DIRECTLY against
/// `localDofIndex(node, component)`, rather than only through a strain. The
/// four entries are the element's nodes in the order the mesh stores them --
/// NOT reordered by coordinate, which would silently change the sign of the
/// Jacobian for half the elements.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<std::array<Translation3D, kTet4Nodes>>
elementDisplacements(const MeshDofMap& numbering, const SolvedSystem& solution,
                     const std::array<meshing::NodeId, kTet4Nodes>& nodes);

} // namespace bettercad::structural
