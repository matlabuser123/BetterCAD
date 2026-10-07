#pragma once

// P17-ELEM-001 -- the linear-elastic Tet4 element kernel.
//
// THE DERIVATION IS THE SPECIFICATION, and it is written down separately:
// docs/verification/P17-ELEM-001/TET4_DERIVATION.md fixes the reference
// element, the shape functions, the Jacobian convention, the physical-gradient
// transformation, the signed-volume formula, the B convention, the D
// convention, the local DOF ordering and the Ke formula. It was written before
// this header, and the tests check the code against it rather than the other
// way round. Nothing here is free to differ from it.
//
// THREE STATELESS COMPUTATIONS, AND THREE VALUES.
//
//   computeTet4Kinematics(nodes)     -> volume, shape gradients, B
//   isotropicElasticity(constants)   -> D
//   computeTet4Stiffness(kin, elas)  -> Ke = V B^T D B
//
// Split this way because the three have different inputs and different
// lifetimes. Kinematics is a function of GEOMETRY alone, so it survives a
// material edit; D is a function of the MATERIAL alone, so one D serves every
// element of a body; Ke needs both. An assembler computing a hundred thousand
// elements builds D once.
//
// B AND D ARE PUBLIC ON PURPOSE. `P17-POST-001` recovers strain as
// `eps = B u_e` and stress as `sigma = D eps`, and if B were buried in this
// translation unit that milestone would reimplement it -- with its own
// transpose and its own shear convention, which is exactly how a stress field
// ends up wrong in the shear terms only. `Tet4Kinematics::strainFrom` and
// `ElasticityMatrix::stressFrom` are the shared path, and they are here so
// there is one of each in the tree.
//
// NO EIGEN, DELIBERATELY. See README.md in the evidence directory: Eigen is a
// PRIVATE_LINK of `sketch` and `assembly` and appears in no public header in
// the repository, so these types could not be Eigen types anyway; and
// src/structural/CMakeLists.txt records that admitting Eigen to this module is
// `P17-SOLVE-001`'s decision and carries a licence question the owner must
// answer. What this file needs is a 3x3 determinant and inverse and two fixed
// products -- arithmetic, not a library. The TESTS use Eigen, for
// eigenvalues, rank and an independent reference implementation that
// deliberately takes a different route.
//
// WHAT THIS FILE DOES NOT DO. No global assembly, no loads, no restraints, no
// solve, no stress recovery, no quality policy, no persistence and no material
// database. Those are P17-ASSEMBLY-001, P17-LOAD-001, P17-BC-001,
// P17-SOLVE-001, P17-POST-001, P17-VALID-001, P17-PERSIST-001 and P15.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralData.hpp>
#include <bettercad/structural/StructuralDof.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// Quantities
// ---------------------------------------------------------------------------

/// A shape-function gradient component: `1/m`.
///
/// Dimensioned rather than a bare double, so `B u` is dimensionally a strain
/// and the compiler says so: `(1/m) * m` cancels to `double`, which is what a
/// strain is.
using InverseLength = Quantity<dimensions::length.inverse()>;

/// A stiffness-matrix entry: `N/m`.
///
/// DEFINED HERE RATHER THAN IN core/Units.hpp, and that is a scope decision
/// rather than an oversight. `Force3D` went into `core/math` in P17-DATA-001
/// because `Translation3D` was already its sibling there; `N/m` has no sibling
/// and no second consumer in the tree, and adding an alias to `core` would put
/// every module in this milestone's blast radius for a name only `structural`
/// uses. If a second consumer appears, ADR-027's canonical set is where it
/// moves to.
using Stiffness = Quantity<dimensions::force / dimensions::length>;

/// The dimensional reasoning of `Ke = V B^T D B`, checked by the compiler
/// rather than asserted in a comment.
///
/// ```text
///     V  m^3    B  1/m    D  N/m^2
///     m^3 . (1/m) . (N/m^2) . (1/m)  =  N/m
/// ```
static_assert(decltype(Volume{} * InverseLength{} * Stress{} *
                       InverseLength{})::dimension == dimensions::force / dimensions::length,
              "Ke = V B^T D B must come out in N/m");
static_assert(Stiffness::dimension == dimensions::force / dimensions::length);
/// And a strain really is dimensionless, so `B u` needs no conversion.
static_assert(std::is_same_v<decltype(InverseLength{} * Length{}), double>);

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

/// Nodes per Tet4. Four, and it is not a parameter: `ElementType` ships Tet4
/// only and `nodeCount(Tetrahedron4)` is P16's authority on the arity.
inline constexpr std::size_t kTet4Nodes = 4;

/// Degrees of freedom of one Tet4: `kDofsPerNode * kTet4Nodes`.
///
/// Expressed through P17-DOF's own constant, so a change there cannot leave
/// this one stale.
inline constexpr std::size_t kTet4Dofs = kDofsPerNode * kTet4Nodes;

/// Rows of `B`, and components of a Voigt strain or stress. Taken from
/// P17-DATA's `kTensorComponents`, never written as 6.
inline constexpr std::size_t kVoigtComponents = kTensorComponents.size();

static_assert(kTet4Dofs == 12);
static_assert(kVoigtComponents == 6);

/// Where the degree of freedom of @p node and @p component sits in a local
/// element vector or in a row or column of `Ke`.
///
/// ```text
///     0  node 0 Ux      3  node 1 Ux      6  node 2 Ux      9  node 3 Ux
///     1  node 0 Uy      4  node 1 Uy      7  node 2 Uy     10  node 3 Uy
///     2  node 0 Uz      5  node 1 Uz      8  node 2 Uz     11  node 3 Uz
/// ```
///
/// INTERLEAVED PER NODE, which is ADR-037's frozen global convention and must
/// be the local one too or an assembler would scatter an element's twelve
/// entries into the wrong rows. Written in terms of `kDofsPerNode` and
/// `offsetOf` -- both P17-DOF's -- so the two orderings cannot drift apart,
/// and asserted index by index in
/// `Tet4Element_LocalDofOrderingMatchesTheGlobalNumbering`.
[[nodiscard]] constexpr std::size_t localDofIndex(std::size_t node,
                                                  DofComponent component) noexcept {
    return kDofsPerNode * node + offsetOf(component);
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a tetrahedron or a material cannot be made into an element.
///
/// THE FIRST FOUR ARE P16'S RULE, IN P16'S ORDER, WITH P16'S CLASSIFICATION.
/// `MeshValidation` refuses a non-finite signed volume as *degenerate* rather
/// than inventing a separate kind, with its own reason -- "a non-finite
/// determinant is never evidence of a valid element, so it is refused rather
/// than compared against zero, a comparison an infinity would answer
/// 'greater'" -- and this follows it rather than deciding differently.
///
/// EVERY VALUE IS REACHABLE, and each has a test. That was checked
/// deliberately, because P17-ARCH-001 found three unreachable values in its own
/// first draft and deleted them.
enum class ElementProblem : std::uint8_t {
    /// A node coordinate is NaN or infinite. P16 never publishes one; a
    /// synthetic fixture can, and the kernel refuses before any arithmetic.
    NonFiniteCoordinate,
    /// The signed volume is exactly zero -- the four nodes are coplanar -- or
    /// it is not finite, which is the overflow case P16 classifies here too.
    DegenerateElement,
    /// The signed volume is negative: the nodes are ordered the wrong way
    /// round. REFUSED, NEVER REORDERED. P16 owns mesh correction (its Netgen
    /// adapter already applies one swap); P17 refuses invalid solver input,
    /// because a silent reorder destroys the evidence that something upstream
    /// was wrong.
    InvertedElement,
    /// A shape gradient or a stiffness entry did not come out finite although
    /// the inputs were valid: a positive but tiny determinant whose reciprocal
    /// overflows. A RESULT check, not an invented input tolerance -- P16's
    /// degeneracy rule has no tolerance and this milestone does not add one.
    NonFiniteResult,
    /// The elastic constants do not yield a finite `lambda` or `mu`: a Poisson
    /// ratio at exactly -1 or 0.5, or a non-finite input.
    ///
    /// THE ADMISSIBLE RANGE IS NOT RE-CHECKED HERE. P15 refuses an unusable
    /// `E` or `nu` at the point of entry, which P17-MAT-001 established and
    /// proved, so a material reaching this kernel through
    /// `StructuralMaterial::elastic()` cannot be out of range. What is checked
    /// is finiteness, because `LinearElasticConstants` is a plain struct a
    /// caller can fill by hand, and a NaN must not reach `Ke` silently.
    InvalidMaterial,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(ElementProblem problem) noexcept;

// ---------------------------------------------------------------------------
// Kinematics: volume, gradients, B
// ---------------------------------------------------------------------------

/// The constant kinematics of one valid Tet4.
///
/// A Tet4's shape functions are linear, so every gradient is constant over the
/// element and so is `B`. There is no quadrature loop and none is needed:
/// `Ke = V B^T D B` is exact.
///
/// POSSESSION IS THE EVIDENCE that the geometry is valid. There is no public
/// constructor and one friend, so a function taking a `const Tet4Kinematics&`
/// knows the volume is positive and finite and the gradients are finite --
/// the pattern `VolumeMesh`, `StructuralModel` and `MeshDofMap` already use
/// (ADR-036).
///
/// A VALUE, NOT A VIEW: it holds twelve gradient components and a volume, and
/// borrows nothing, so it cannot outlive its data.
class BETTERCAD_STRUCTURAL_EXPORT Tet4Kinematics {
public:
    /// The element's volume. Positive and finite, by construction.
    ///
    /// `V = det J / 6`, which is character-for-character the expression
    /// `meshing::signedVolume` computes -- so this is P16's signed volume, not
    /// a second definition of it, and
    /// `Tet4Element_VolumeAgreesWithTheMeshingSignedVolume` asserts it.
    [[nodiscard]] Volume volume() const noexcept { return volume_; }

    /// `det J`, the signed determinant of the Jacobian. `6 * volume()`.
    ///
    /// Exposed because the brief's scale law is clearest in terms of it and
    /// because a reader checking the derivation wants it; nothing downstream
    /// needs it.
    [[nodiscard]] Volume determinant() const noexcept { return determinant_; }

    /// `grad Ni` of node @p node, in `1/m`, as `(dNi/dx, dNi/dy, dNi/dz)`.
    [[nodiscard]] std::array<InverseLength, 3> gradient(std::size_t node) const noexcept;

    /// `B(row, column)`, in `1/m`. Rows are `exx eyy ezz gxy gyz gzx`
    /// (P17-DATA's `TensorComponent` order); columns are
    /// `localDofIndex(node, component)`.
    ///
    /// Out of range returns zero rather than reading past the end: a `B` entry
    /// that does not exist is zero, and the caller's loop bounds are the
    /// caller's business.
    [[nodiscard]] InverseLength b(std::size_t row, std::size_t column) const noexcept;

    /// `eps = B u_e` -- the shared strain-recovery path.
    ///
    /// `P17-POST-001` calls THIS rather than rebuilding `B`, which is why it
    /// is public. The displacements are in node order, so
    /// `displacements[i]` belongs to the element's node `i`.
    [[nodiscard]] Strain6
    strainFrom(const std::array<Translation3D, kTet4Nodes>& displacements) const noexcept;

    friend bool operator==(const Tet4Kinematics&, const Tet4Kinematics&) = default;

private:
    Tet4Kinematics() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<Tet4Kinematics>
    computeTet4Kinematics(const std::array<Point3D, kTet4Nodes>& nodes);

    Volume volume_{};
    Volume determinant_{};
    /// `gradients_[node][axis]`, SI.
    std::array<std::array<double, 3>, kTet4Nodes> gradients_{};
};

/// Computes the kinematics of the tetrahedron whose corners are @p nodes, in
/// the order they are stored.
///
/// Read-only and stateless: no cache, no global, nothing mutable. The node
/// order is NOT changed -- an inverted tetrahedron is refused, because P16 owns
/// mesh correction and this kernel owns refusing invalid input.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<Tet4Kinematics>
computeTet4Kinematics(const std::array<Point3D, kTet4Nodes>& nodes);

/// The problem `computeTet4Kinematics` would report, or none if it would
/// succeed. Same checks, same order, for a caller that wants to count or
/// present the reason rather than parse a message.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<ElementProblem>
tet4GeometryProblem(const std::array<Point3D, kTet4Nodes>& nodes) noexcept;

// ---------------------------------------------------------------------------
// Constitutive: D
// ---------------------------------------------------------------------------

/// The isotropic linear-elastic constitutive matrix, `6 x 6`, in `Pa`.
///
/// ```text
///     lambda = E nu / ((1 + nu)(1 - 2 nu))
///     mu     = E / (2 (1 + nu))
/// ```
///
/// `mu` ON THE SHEAR DIAGONAL, NOT `2 mu`. With engineering shear the factor of
/// two already lives in `gamma`, so `tau_xy = mu gamma_xy`. The other choice
/// doubles every shear stress and leaves the normal terms right, which is the
/// hardest kind of defect to see -- so it has its own regression test and its
/// own mutation probe.
///
/// `mu` IS NOT RECOMPUTED. `LinearElasticConstants::shearModulus` already is
/// `E / (2(1 + nu))`, derived by P15 with exactly this formula, so this type
/// carries P15's value. `lambda` is not provided and is computed from the
/// frozen formula; the identity `lambda = K - 2 mu / 3` cross-checks it
/// against P15's other derived constant.
class BETTERCAD_STRUCTURAL_EXPORT ElasticityMatrix {
public:
    /// `D(row, column)`. Out of range returns zero.
    [[nodiscard]] Stress d(std::size_t row, std::size_t column) const noexcept;

    /// The first Lame parameter.
    [[nodiscard]] ElasticModulus lame() const noexcept { return lame_; }
    /// The second, which is the shear modulus P15 derived.
    [[nodiscard]] ElasticModulus shear() const noexcept { return shear_; }

    /// `sigma = D eps` -- the shared stress path. `P17-POST-001` calls THIS
    /// rather than rebuilding `D`.
    [[nodiscard]] Stress6 stressFrom(const Strain6& strain) const noexcept;

    friend bool operator==(const ElasticityMatrix&, const ElasticityMatrix&) = default;

private:
    ElasticityMatrix() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<ElasticityMatrix>
    isotropicElasticity(const materials::LinearElasticConstants& constants);

    ElasticModulus lame_{};
    ElasticModulus shear_{};
};

/// Builds `D` from resolved elastic constants.
///
/// Fails with `InvalidMaterial` only when `lambda` or `mu` is not finite. The
/// admissible range is P15's: `createMaterial` and `setMaterialMechanical`
/// both refuse an unusable `E` or `nu` at entry, so a material that reached
/// here through `StructuralMaterial::elastic()` cannot be out of range, and
/// re-deriving the range check would be the duplication P17-MAT-001 was
/// written to avoid.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<ElasticityMatrix>
isotropicElasticity(const materials::LinearElasticConstants& constants);

// ---------------------------------------------------------------------------
// Stiffness: Ke
// ---------------------------------------------------------------------------

/// One element's stiffness matrix, `12 x 12`, in `N/m`.
///
/// Symmetric because `B^T D B` is, with `D` symmetric -- NOT because it is
/// symmetrised afterwards. There is no `0.5 * (K + K^T)` anywhere in the
/// implementation, and a mutation probe confirms that adding one would hide a
/// formula error rather than fix a numerical one.
///
/// A free Tet4's `Ke` is positive SEMIdefinite with a six-dimensional null
/// space -- three translations and three infinitesimal rotations. Nothing here
/// regularises the diagonal to make it definite; that would delete the
/// rigid-body modes, and any regularisation is a later solver's policy rather
/// than element physics.
class BETTERCAD_STRUCTURAL_EXPORT Tet4Stiffness {
public:
    /// `Ke(row, column)`. Out of range returns zero.
    [[nodiscard]] Stiffness operator()(std::size_t row, std::size_t column) const noexcept;

    /// `f = Ke u_e`, the nodal force a displacement field implies.
    [[nodiscard]] std::array<Force3D, kTet4Nodes>
    forceFrom(const std::array<Translation3D, kTet4Nodes>& displacements) const noexcept;

    /// `U = (1/2) u^T Ke u`. Zero for a rigid-body motion, positive for any
    /// deformation of a valid element.
    [[nodiscard]] Energy
    energyFrom(const std::array<Translation3D, kTet4Nodes>& displacements) const noexcept;

    friend bool operator==(const Tet4Stiffness&, const Tet4Stiffness&) = default;

private:
    Tet4Stiffness() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<Tet4Stiffness>
    computeTet4Stiffness(const Tet4Kinematics& kinematics, const ElasticityMatrix& elasticity);

    /// Row-major, SI.
    std::array<double, kTet4Dofs * kTet4Dofs> entries_{};
};

/// `Ke = V B^T D B`.
///
/// Exact rather than integrated, because both factors are constant over a
/// Tet4. Fails with `NonFiniteResult` if any entry does not come out finite,
/// which valid inputs cannot cause and a pathological fixture can.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<Tet4Stiffness>
computeTet4Stiffness(const Tet4Kinematics& kinematics, const ElasticityMatrix& elasticity);

/// The whole chain for one tetrahedron, for a caller that wants `Ke` and
/// nothing else. Equivalent to the three calls above in order.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<Tet4Stiffness>
computeTet4Stiffness(const std::array<Point3D, kTet4Nodes>& nodes,
                     const materials::LinearElasticConstants& constants);

} // namespace bettercad::structural
