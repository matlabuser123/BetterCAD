#pragma once

// P18-ELEM-001 -- the linear Tet4 thermal element kernel.
//
// THE FORMULATION IS THE SPECIFICATION, and it is written down separately:
// docs/verification/P18-ELEM-001/FORMULATION.md fixes the reference element,
// the shape functions, the Jacobian convention, the signed-volume formula, the
// gradient mapping, the G convention, the local node and temperature order,
// the orientation and degeneracy policy, the Ke formula, the Ce formula and
// the capacity policy. It was written before this header, and the tests check
// the code against it rather than the other way round.
//
// THREE STATELESS COMPUTATIONS, AND THREE VALUES.
//
//   computeTet4Geometry(corners)              -> V, G
//   computeTet4Conduction(geometry, k)        -> Ke = V k G^T G      [W/K]
//   computeTet4Capacity(geometry, {rho, cp})  -> Ce = rho cp V/20 M  [J/K]
//
// Split this way because a STEADY analysis holds a material whose capacity is
// empty (P18-MAT-001: "requiring a density for K T = Q would block a perfectly
// solvable steady problem") and must form Ke without one, and because a
// transient analysis forms both from ONE geometry. There is no combined call
// that could let a missing rho block a valid Ke, and nothing computes Ce on
// the steady path.
//
// THE CAPACITY POLICY IS CONSISTENT, EXACT AND FROZEN. Ce is the closed-form
// integral of rho cp N^T N over the element, diag rho cp V/10 and off-diagonal
// rho cp V/20. It is not integrated numerically: Ni Nj is quadratic, so a
// one-point centroid rule gives rho cp V/16 in every entry -- a rank-one
// matrix with the right row sums and the wrong diagonal -- and that is the
// trap the capacity tests pin the full matrix against. Lumping is not
// provided, not optional and not hidden behind a setting; ADR-049 asked for
// one stated policy and FORMULATION.md records why this one.
//
// NO EIGEN, DELIBERATELY. src/thermal/CMakeLists.txt records that no linear
// algebra is linked and that admitting it is P18-SOLVE-001's decision. What
// this file needs is a 3x3 determinant and inverse and two 4x4 products --
// arithmetic, not a library. The TESTS use Eigen, for eigenvalues, rank and
// an independent reference implementation that deliberately takes a
// different route (the 4x4 [1 x y z] inverse, never a Jacobian).
//
// WHAT THIS FILE DOES NOT DO. No global assembly, no NodeId or ThermalDofIndex
// arithmetic -- a 4x4 matrix is 4x4 whatever the global numbering -- no load,
// no boundary condition, no solve, no flux recovery, no quality policy, no
// persistence, no anisotropy, no temperature-dependent property, no cache and
// no static state. Those are P18-ASSEMBLY-001, P18-LOAD-001, P18-BC-001,
// P18-SOLVE-001, P18-POST-001, P18-VALID-001, P18-PERSIST-001, and ADR-049's
// refused scope.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalMaterial.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Quantities
// ---------------------------------------------------------------------------

/// A shape-function gradient component: `1/m`.
///
/// Dimensioned rather than a bare double, so `G Te` is dimensionally a
/// temperature gradient and the compiler says so.
using InverseLength = Quantity<dimensions::length.inverse()>;

/// A conduction-matrix entry: `W/K`.
///
/// Defined here rather than in core/Units.hpp for the reason P17-ELEM-001
/// gave for `Stiffness`: there is no second consumer in the tree, and adding
/// a core alias would put every module in this milestone's blast radius.
using ThermalConductance = Quantity<dimensions::power / dimensions::temperature>;

/// A capacity-matrix entry: `J/K`. Likewise.
using HeatCapacity = Quantity<dimensions::energy / dimensions::temperature>;

/// The dimensional reasoning of the two formulas, checked by the compiler
/// rather than asserted in a comment.
///
/// ```text
///     Ke = V k G^T G      m^3 . W/(m K) . (1/m) . (1/m)        =  W/K
///     Ce = rho cp V M     kg/m^3 . J/(kg K) . m^3              =  J/K
///     grad T = G Te       (1/m) . K                            =  K/m
///     Ke Te               (W/K) . K                            =  W
/// ```
static_assert(decltype(Volume{} * ThermalConductivity{} * InverseLength{} *
                       InverseLength{})::dimension == dimensions::power / dimensions::temperature,
              "Ke = V k G^T G must come out in W/K");
static_assert(decltype(Density{} * SpecificHeatCapacity{} * Volume{})::dimension ==
                  dimensions::energy / dimensions::temperature,
              "Ce = rho cp V must come out in J/K");
static_assert(decltype(InverseLength{} * Temperature{})::dimension ==
                  dimensions::temperatureGradient,
              "grad T = G Te must come out in K/m");
static_assert(decltype(ThermalConductance{} * Temperature{})::dimension == dimensions::power,
              "Ke Te must come out in W");
static_assert(ThermalConductance::dimension == dimensions::power / dimensions::temperature);
static_assert(HeatCapacity::dimension == dimensions::energy / dimensions::temperature);
/// And W/K and J/K are DIFFERENT dimensions, so a conduction entry cannot be
/// handed where a capacity entry is wanted.
static_assert(ThermalConductance::dimension != HeatCapacity::dimension);

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

/// Nodes per Tet4. Four, and it is not a parameter: `ElementType` ships Tet4
/// only and `meshing::nodeCount(Tetrahedron4)` is P16's authority on the
/// arity; a test asserts the two agree.
inline constexpr std::size_t kTet4Nodes = 4;

/// Temperature unknowns of one Tet4: one scalar per node (ADR-049), so four.
///
/// Written as `kTet4Nodes` rather than through P18-DOF-001's
/// `kThermalDofsPerNode` DELIBERATELY: this kernel does not include the global
/// numbering, because a local 4x4 matrix does not depend on it. A test
/// includes both headers and asserts `kTet4ThermalDofs == kThermalDofsPerNode
/// * kTet4Nodes`, so the two cannot drift apart without a failure.
inline constexpr std::size_t kTet4ThermalDofs = kTet4Nodes;

/// The capacity policy this kernel implements, named so a later assembler
/// can record which formulation its `C` carries. One value; there is no
/// alternative and no setting (FORMULATION.md).
inline constexpr std::string_view kTet4CapacityPolicy = "consistent";

/// The four corners of one tetrahedron, in the element's own node order.
using Tet4Corners = std::array<Point3D, kTet4Nodes>;

/// The four nodal temperatures of one tetrahedron, in the same order.
using Tet4Temperatures = std::array<Temperature, kTet4Nodes>;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a tetrahedron or a material cannot be made into a thermal element.
///
/// THE FIRST FOUR ARE P16'S RULE, IN P16'S ORDER, WITH P16'S CLASSIFICATION,
/// exactly as P17-ELEM-001 adopted them: a non-finite signed volume is
/// *degenerate* rather than its own kind, because "a non-finite determinant
/// is never evidence of a valid element, so it is refused rather than compared
/// against zero -- a comparison an infinity would answer 'greater'".
///
/// The last three are the material contract of the two formulas, each a
/// different thing for a caller to fix, and each a different field so that a
/// density offered as a specific heat is named as the wrong one.
///
/// EVERY VALUE IS REACHABLE, and each has a test.
enum class ElementProblem : std::uint8_t {
    /// A corner coordinate is NaN or infinite. P16 never publishes one; a
    /// synthetic fixture can, and the kernel refuses before any arithmetic.
    NonFiniteCoordinate,
    /// The four corners are coplanar or two coincide, or the signed volume is
    /// not finite (the overflow case P16 classifies here too).
    ///
    /// "COPLANAR" IS DECIDED BY A BOUND, NOT BY EXACT ZERO -- the one place
    /// this kernel refines P16's and P17's rule. A computed `|det J|` no
    /// larger than `16 eps |e1||e2||e3|` (the three edge lengths from corner
    /// 1) is rounding noise: it is what the arithmetic produces from a true
    /// zero, and its sign carries no information. The bound is homogeneous
    /// of degree three in the coordinates, as `det J` is, so it is
    /// scale-aware rather than an absolute constant, and it is twelve orders
    /// of magnitude below a 1000:1 sliver. Derivation: FORMULATION.md.
    DegenerateElement,
    /// The signed volume is negative: the corners are ordered the wrong way
    /// round. REFUSED, NEVER REORDERED. P16 owns mesh correction; this kernel
    /// owns refusing invalid input, because a silent reorder destroys the
    /// evidence that something upstream was wrong.
    InvertedElement,
    /// A gradient or a matrix entry did not come out finite although the
    /// inputs passed their own checks: a positive but subnormal determinant
    /// whose reciprocal overflows, or a `rho cp V` product that overflows. A
    /// RESULT check, not an invented input tolerance.
    NonFiniteResult,
    /// `k` is not finite or not positive. No `abs`, no `max(k, eps)`.
    InvalidConductivity,
    /// `rho` is not finite or not positive.
    InvalidDensity,
    /// `cp` is not finite or not positive.
    InvalidSpecificHeat,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ElementProblem problem) noexcept;

// ---------------------------------------------------------------------------
// Geometry: volume and G
// ---------------------------------------------------------------------------

/// The constant geometry of one valid Tet4: its volume and its shape-function
/// gradient matrix `G`.
///
/// A Tet4's shape functions are linear, so every gradient is constant over
/// the element and so is `G`. There is no quadrature loop and none is needed.
///
/// POSSESSION IS THE EVIDENCE that the geometry is valid. There is no public
/// constructor and one friend, so a function taking a `const Tet4Geometry&`
/// knows the volume is positive and finite and the gradients are finite --
/// the pattern `VolumeMesh`, `ThermalDofMap` and P17's `Tet4Kinematics` use
/// (ADR-036, ADR-051).
///
/// A VALUE, NOT A VIEW: twelve gradient components and two volumes, borrowing
/// nothing. It carries NO node handles and NO unknown indices: which global
/// rows these four local rows scatter into is P18-ASSEMBLY-001's, through
/// P18-DOF-001's `elementDofs`, which returns the same element order.
class BETTERCAD_THERMAL_EXPORT Tet4Geometry {
public:
    /// The element's volume. Positive and finite, by construction.
    ///
    /// `V = det J / 6`, which is character-for-character the expression
    /// `meshing::signedVolume` computes -- so this is P16's signed volume, not
    /// a second definition of it, and a test asserts the two agree bit for
    /// bit on every fixture.
    [[nodiscard]] Volume volume() const noexcept { return volume_; }

    /// `det J`, the signed determinant of the Jacobian. `6 * volume()`.
    [[nodiscard]] Volume determinant() const noexcept { return determinant_; }

    /// `grad Ni` of corner @p node, in `1/m`, as `(dNi/dx, dNi/dy, dNi/dz)`.
    /// Out of range returns zeros.
    [[nodiscard]] std::array<InverseLength, 3> gradient(std::size_t node) const noexcept;

    /// `G(row, column)`, in `1/m`: row is the axis (0 x, 1 y, 2 z), column is
    /// the corner. `G` is `3 x 4`, and `grad T = G Te`.
    ///
    /// Out of range returns zero rather than reading past the end: an entry
    /// that does not exist is zero, and the caller's loop bounds are the
    /// caller's business.
    [[nodiscard]] InverseLength g(std::size_t row, std::size_t column) const noexcept;

    /// `grad T = G Te`, in `K/m` -- the shared gradient-recovery path.
    ///
    /// `P18-POST-001` will call THIS rather than rebuilding `G`, which is why
    /// it is public and returns P18-DATA-001's `TemperatureGradient3`. The
    /// temperatures are in corner order, so `temperatures[i]` belongs to the
    /// element's corner `i`.
    [[nodiscard]] TemperatureGradient3 gradientFrom(const Tet4Temperatures& temperatures) const noexcept;

    friend bool operator==(const Tet4Geometry&, const Tet4Geometry&) = default;

private:
    Tet4Geometry() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<Tet4Geometry> computeTet4Geometry(const Tet4Corners& corners);

    Volume volume_{};
    Volume determinant_{};
    /// `gradients_[node][axis]`, SI.
    std::array<std::array<double, 3>, kTet4Nodes> gradients_{};
};

/// Computes the geometry of the tetrahedron whose corners are @p corners, in
/// the order they are given.
///
/// Read-only and stateless: no cache, no global, nothing mutable. The corner
/// order is NOT changed -- an inverted tetrahedron is refused, because P16
/// owns mesh correction and this kernel owns refusing invalid input.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tet4Geometry>
computeTet4Geometry(const Tet4Corners& corners);

/// The problem `computeTet4Geometry` would report, or none if it would
/// succeed. Same checks, same order, for a caller that wants to count or
/// present the reason rather than parse a message.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<ElementProblem>
tet4GeometryProblem(const Tet4Corners& corners) noexcept;

/// The corner positions of @p tetrahedron in @p mesh, in the element's OWN
/// node order -- the order P18-DOF-001's `elementDofs` returns the unknowns
/// in, so row `i` of the local matrices is unknown `i` of that array.
///
/// The one place a mesh handle meets a coordinate in this kernel, and it
/// exists so a caller cannot pair the coordinates of one tetrahedron with
/// the connectivity of another: the positions are read from the mesh the
/// element belongs to, by identity. Fails with `NotFound` naming the handle
/// if the tetrahedron names a node the mesh does not have.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tet4Corners>
cornersOf(const meshing::Mesh& mesh, const meshing::Tetrahedron& tetrahedron);

// ---------------------------------------------------------------------------
// Conduction: Ke
// ---------------------------------------------------------------------------

/// One element's conduction matrix, `4 x 4`, in `W/K`.
///
/// `Ke = V k G^T G`, so `Ke_ij = k V (grad Ni . grad Nj)`. Symmetric because
/// the dot product is -- NOT because it is symmetrised afterwards. There is
/// no `0.5 * (K + K^T)` anywhere in the implementation.
///
/// A free Tet4's `Ke` is positive SEMIdefinite with a one-dimensional null
/// space -- the uniform temperature. Only temperature DIFFERENCES conduct.
/// Nothing here regularises the diagonal to make it definite; that would give
/// a uniform temperature a heat flow, and any regularisation is a later
/// solver's policy rather than element physics.
class BETTERCAD_THERMAL_EXPORT Tet4Conduction {
public:
    /// `Ke(row, column)`. Out of range returns zero.
    [[nodiscard]] ThermalConductance operator()(std::size_t row, std::size_t column) const noexcept;

    /// `Ke Te`, in `W`: the nodal heat flow a temperature field implies. Zero
    /// for a uniform field. Its four entries sum to zero, because conduction
    /// redistributes heat inside the element and creates none.
    [[nodiscard]] std::array<Power, kTet4Nodes>
    heatFlowFrom(const Tet4Temperatures& temperatures) const noexcept;

    friend bool operator==(const Tet4Conduction&, const Tet4Conduction&) = default;

private:
    Tet4Conduction() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<Tet4Conduction>
    computeTet4Conduction(const Tet4Geometry& geometry, ThermalConductivity conductivity);

    /// Row-major, SI.
    std::array<double, kTet4ThermalDofs * kTet4ThermalDofs> entries_{};
};

/// The problem a conductivity would cause, or none: not finite, or not
/// positive, is `InvalidConductivity`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<ElementProblem>
conductivityProblem(ThermalConductivity conductivity) noexcept;

/// `Ke = V k G^T G`.
///
/// Exact rather than integrated, because both factors are constant over a
/// Tet4. Takes the geometry and `k` ONLY: no density, no specific heat, no
/// material object, so the steady path never needs the transient pair.
///
/// Fails with `InvalidConductivity` for a non-finite or non-positive `k`
/// (P15 refuses one at entry, so a `ThermalMaterial::conductivity()` cannot
/// be either; a hand-built `ThermalConductivity` can), and with
/// `NonFiniteResult` if any entry does not come out finite.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tet4Conduction>
computeTet4Conduction(const Tet4Geometry& geometry, ThermalConductivity conductivity);

/// The whole chain for one tetrahedron, for a caller that wants `Ke` and
/// nothing else. Equivalent to the two calls above in order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tet4Conduction>
computeTet4Conduction(const Tet4Corners& corners, ThermalConductivity conductivity);

// ---------------------------------------------------------------------------
// Capacity: Ce
// ---------------------------------------------------------------------------

/// One element's CONSISTENT capacity matrix, `4 x 4`, in `J/K`.
///
/// ```text
///     Ce = rho cp V / 20 * | 2 1 1 1 |      Ce_ii = rho cp V / 10
///                          | 1 2 1 1 |      Ce_ij = rho cp V / 20
///                          | 1 1 2 1 |
///                          | 1 1 1 2 |
/// ```
///
/// The exact integral of `rho cp N^T N` over the element, written in closed
/// form. Symmetric positive definite: eigenvalues `rho cp V / 4` once and
/// `rho cp V / 20` three times, rank four, every off-diagonal positive. A
/// uniform temperature RATE stores heat -- `Ce [1 1 1 1]^T` is not zero and
/// sums to `rho cp V` -- which is the one way `Ce` and `Ke` differ in kind.
///
/// NOT LUMPED, and not lumpable through this type: there is no diagonal
/// variant, no setting and no policy enum. `kTet4CapacityPolicy` names what
/// this is.
class BETTERCAD_THERMAL_EXPORT Tet4Capacity {
public:
    /// `Ce(row, column)`. Out of range returns zero.
    [[nodiscard]] HeatCapacity operator()(std::size_t row, std::size_t column) const noexcept;

    /// `ones^T Ce ones`: the element's total heat capacity, `rho cp V`.
    ///
    /// Derived from the entries, never stored beside them, so it cannot
    /// disagree with the matrix it summarises.
    [[nodiscard]] HeatCapacity total() const noexcept;

    friend bool operator==(const Tet4Capacity&, const Tet4Capacity&) = default;

private:
    Tet4Capacity() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<Tet4Capacity>
    computeTet4Capacity(const Tet4Geometry& geometry, const ThermalCapacity& capacity);

    /// Row-major, SI.
    std::array<double, kTet4ThermalDofs * kTet4ThermalDofs> entries_{};
};

/// The problem a capacity pair would cause, or none. `rho` is checked first:
/// not finite or not positive is `InvalidDensity`; then `cp`, likewise
/// `InvalidSpecificHeat`. Each field is named separately, so a density
/// offered as a specific heat is reported as the wrong one.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<ElementProblem>
capacityProblem(const ThermalCapacity& capacity) noexcept;

/// `Ce = rho cp V / 20 * M`, the consistent matrix above.
///
/// Takes the geometry and the `{rho, cp}` pair ONLY: no conductivity, so the
/// capacity can be qualified independently of the conduction and a `k` can
/// never enter it by mistake. The pair is P18-MAT-001's `ThermalCapacity`,
/// present on a resolved material exactly when the mode is transient.
///
/// Fails with `InvalidDensity` or `InvalidSpecificHeat` for a non-finite or
/// non-positive input, and with `NonFiniteResult` if `rho cp V` or any entry
/// does not come out finite -- two finite positives can overflow.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tet4Capacity>
computeTet4Capacity(const Tet4Geometry& geometry, const ThermalCapacity& capacity);

/// The whole chain for one tetrahedron, for a caller that wants `Ce` and
/// nothing else.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tet4Capacity>
computeTet4Capacity(const Tet4Corners& corners, const ThermalCapacity& capacity);

} // namespace bettercad::thermal
