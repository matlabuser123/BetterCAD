#pragma once

// P18-BC-001 -- the local convection (Robin) boundary term of one linear
// triangle.
//
// THE BOUNDARY LAW, FROZEN HERE AND SHARED BY EVERY LATER MILESTONE:
//
//     -k grad(T) . n  =  h (T - T_inf)            n = the OUTWARD unit normal
//
// The left side is the conductive heat flux LEAVING the body through the face;
// the right side is the convective loss to the ambient. So a face hotter than
// its ambient (T > T_inf) loses heat, a face colder than its ambient gains it,
// and a face at its ambient exchanges none. This is the same law P18-DATA-001
// wrote for `ConvectionBoundary` (`q = h (T_ambient - T)`, positive into the
// body) read from the other side; WEAK_FORM.md derives the equivalence.
//
// THE WEAK FORM, WHICH IS WHY THERE ARE TWO TERMS AND NOT ONE. Multiply the
// conduction equation by a test function N and integrate by parts; the
// boundary integral that appears is `integral(N k grad(T).n dA)`, and the Robin
// law replaces `k grad(T).n` by `-h (T - T_inf)`:
//
//     boundary term  =  - integral(N h T dA)  +  integral(N h T_inf dA)
//
// Moving the part that contains the unknown T to the left, the system becomes
// `(K + Kconv) T = Q + Qconv` with
//
//     Kconv  =  integral( h N^T N dA )         [W/K]    on the LEFT, adds to K
//     Qconv  =  integral( h T_inf N^T dA )     [W]      on the RIGHT, adds to Q
//
// BOTH TERMS, ALWAYS. A convection face with only its source is a heat source
// with no way to relax; with only its matrix it is a sink that cools toward
// 0 K. Neither is convection. And neither contains the conductivity: k appears
// in the strong law and is consumed by the substitution, so the boundary terms
// are functions of h, T_inf and the facet and of nothing else.
//
// EXACT, NOT QUADRATURE. For a constant h on a three-node linear triangle the
// integrals are closed forms,
//
//     Kconv  =  (h A / 12) | 2 1 1 |        Qconv  =  (h T_inf A / 3) | 1 |
//                          | 1 2 1 |                                  | 1 |
//                          | 1 1 2 |                                  | 1 |
//
// and the matrix is CONSISTENT. A one-point centroid rule would give every
// entry h A / 9 (same row sums, wrong interpolation); a lumped rule would give
// (h A / 3) I (same row sums, no coupling). Row sums alone cannot tell the
// three apart, which is why the tests compare the whole matrix. The same trap
// P18-ELEM-001 closed for the consistent capacity matrix.
//
// POSITIVE SEMIDEFINITE AND SYMMETRIC BY CONSTRUCTION. The eigenvalues are
// `h A / 3` once and `h A / 12` twice, so for `h > 0` the facet matrix is
// positive definite on the three boundary shape functions. (The assembled
// system's singularity is the solver's question, not this one's.)
//
// WINDING DOES NOT MATTER. Both terms depend on the unsigned area, so reversing
// a facet's vertex order cannot change either, and no normal is computed.
//
// WHAT THIS FILE DOES NOT DO. No resolution of a CAD face (ThermalBoundary.hpp),
// no scatter into a global matrix (P18-ASSEMBLY-001), no solve, no post-processed
// convective heat rate from a solved field (P18-BALANCE-001), no radiation, no
// time dependence.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalElement.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Units, checked by the compiler
// ---------------------------------------------------------------------------

/// ```text
///     Kconv = h A / 12 ...      W/(m^2 K) . m^2        =  W/K
///     Qconv = h T_inf A / 3     W/(m^2 K) . K . m^2    =  W
///     Kconv T                   (W/K) . K              =  W
/// ```
static_assert(decltype(HeatTransferCoefficient{} * Area{})::dimension ==
                  dimensions::power / dimensions::temperature,
              "h A must come out in W/K");
static_assert(decltype(HeatTransferCoefficient{} * Area{})::dimension == ThermalConductance::dimension,
              "a convection entry is a conduction entry's unit");
static_assert(decltype(HeatTransferCoefficient{} * Temperature{} * Area{})::dimension == dimensions::power,
              "h T_inf A must come out in W");
static_assert(HeatTransferCoefficient::dimension != ThermalConductivity::dimension,
              "h is not k: the boundary terms must not be fed a conductivity");

/// Nodes of a boundary triangle. Three, as P16's linear surface triangle.
inline constexpr std::size_t kTri3Nodes = 3;

/// The three nodal temperatures of one boundary triangle, in the triangle's own
/// node order.
using Tri3Temperatures = std::array<Temperature, kTri3Nodes>;

/// The three corners of one boundary triangle, in the triangle's own node order.
using Tri3Corners = std::array<Point3D, kTri3Nodes>;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a triangle cannot carry a convection term.
enum class ConvectionProblem : std::uint8_t {
    /// The area is zero, negative or not finite. P16 refuses a degenerate
    /// triangle, so this is a defensive check on a result and not an invented
    /// input tolerance. Never a zero contribution.
    InvalidArea,
    /// `h` is NaN, infinite or negative. ZERO IS ACCEPTED HERE: a zero
    /// coefficient is the limit in which the term vanishes, and the kernel is
    /// total over `h >= 0`. (The CANONICAL record refuses zero for a different
    /// reason: see ThermalBoundary.hpp.) A negative `h` would pump heat against
    /// the temperature difference and is not convection; `abs` never repairs it.
    InvalidCoefficient,
    /// The ambient is not a finite absolute temperature (P18-DATA-001's one
    /// rule, `isAbsoluteTemperature`). Checked even when `h` is zero: a
    /// malformed value is malformed whether or not it happens to be multiplied
    /// by nothing.
    InvalidAmbient,
    /// An entry came out infinite or NaN: the product of finite inputs
    /// overflowed. Never accepted.
    NonFiniteResult,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ConvectionProblem problem) noexcept;

// ---------------------------------------------------------------------------
// Tri3Convection
// ---------------------------------------------------------------------------

/// One boundary triangle's `Kconv` (3 x 3, W/K) and `Qconv` (3, W).
///
/// DERIVED, NEVER AUTHORITY, and in the TRIANGLE'S OWN NODE ORDER: row `i` and
/// source `i` belong to corner `i` as P16 stores it, never to a sorted order,
/// so a later scatter through the triangle's `ThermalDofIndex`es cannot pair a
/// row with the wrong unknown.
///
/// The area, `h` and the ambient are kept as the DERIVED record of what the
/// entries were computed from (P18-BALANCE-001 reports them); they are copies
/// of canonical values and are never read back as intent.
class BETTERCAD_THERMAL_EXPORT Tri3Convection {
public:
    /// `Kconv(row, column)`, in W/K. Out of range returns zero.
    [[nodiscard]] ThermalConductance operator()(std::size_t row, std::size_t column) const noexcept;

    /// `Qconv(node)`, in W. Out of range returns zero.
    [[nodiscard]] Power source(std::size_t node) const noexcept;

    [[nodiscard]] Area area() const noexcept { return area_; }
    [[nodiscard]] HeatTransferCoefficient coefficient() const noexcept { return coefficient_; }
    [[nodiscard]] Temperature ambient() const noexcept { return ambient_; }

    /// `Kconv T - Qconv`, in W, for nodal temperatures @p temperatures: the heat
    /// each node LOSES through this face to its ambient, in the convention that
    /// outward is positive. Zero for `T = T_inf` everywhere; for a uniform
    /// `T_b` its three entries sum to `h A (T_b - T_inf)`, positive when the
    /// face is hotter than its ambient and negative when it is colder.
    [[nodiscard]] std::array<Power, kTri3Nodes>
    residualFrom(const Tri3Temperatures& temperatures) const noexcept;

    friend bool operator==(const Tri3Convection&, const Tri3Convection&) = default;

private:
    Tri3Convection() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<Tri3Convection>
    computeTri3Convection(Area area, HeatTransferCoefficient coefficient, Temperature ambient);

    /// Row-major, SI.
    std::array<double, kTri3Nodes * kTri3Nodes> entries_{};
    std::array<double, kTri3Nodes> source_{};
    Area area_{};
    HeatTransferCoefficient coefficient_{};
    Temperature ambient_{};
};

/// The problem the inputs would cause, or none. In the order `computeTri3Convection`
/// checks: area, coefficient, ambient.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<ConvectionProblem>
convectionProblem(Area area, HeatTransferCoefficient coefficient, Temperature ambient) noexcept;

/// `Kconv = (h A / 12) [2 1 1; 1 2 1; 1 1 2]` and `Qconv = (h T_inf A / 3) [1 1 1]`.
///
/// Exact rather than integrated, because `h` and `T_inf` are constant over the
/// facet. Takes the area, `h` and the ambient ONLY: no conductivity, no
/// material, no normal. Fails with `InvalidArea`, `InvalidCoefficient`,
/// `InvalidAmbient`, or `NonFiniteResult` if an entry overflows.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tri3Convection>
computeTri3Convection(Area area, HeatTransferCoefficient coefficient, Temperature ambient);

/// The same, with the area taken from the triangle's corners by P16's own
/// `meshing::triangleArea` (unsigned, metres).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<Tri3Convection>
computeTri3Convection(const Tri3Corners& corners, HeatTransferCoefficient coefficient, Temperature ambient);

} // namespace bettercad::thermal
