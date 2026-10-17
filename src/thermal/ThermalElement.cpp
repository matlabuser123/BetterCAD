// The linear Tet4 thermal element kernel (P18-ELEM-001).
//
// EVERY FORMULA HERE IS FIXED BY
// docs/verification/P18-ELEM-001/FORMULATION.md, which was written first.
// The Jacobian's columns are the three edge vectors from corner 0; physical
// gradients are `J^-T grad_xi N`; `det J = (p2-p1) . ((p3-p1) x (p4-p1))`,
// which is the same expression `meshing::signedVolume` computes, grouped the
// same way; `V = det J/6` with NO absolute value anywhere; `G` is 3x4 with the
// axis down the rows and the corner along the columns; `Ke = V k G^T G`
// exactly; and `Ce` is the closed-form consistent matrix, rho cp V/10 on the
// diagonal and rho cp V/20 off it.
//
// NOTHING IS SYMMETRISED, NOTHING IS REGULARISED, NOTHING IS LUMPED AND
// NOTHING IS INTEGRATED NUMERICALLY. `grad Ni . grad Nj` is symmetric, so a
// `0.5 * (K + K^T)` here would hide a formula error rather than fix a
// numerical one; a free Tet4's `Ke` is meant to be singular, with the uniform
// temperature as its one null mode, so adding anything to the diagonal would
// delete the physics; and `Ni Nj` is quadratic, so a centroid rule would give
// the wrong `Ce` while preserving its row sums.
//
// NO HIDDEN STATE. Every function is a pure computation over its arguments:
// no `static` storage, no cache, no `mutable`, no `thread_local`. That makes
// determinism structural and leaves future parallel assembly open.
//
// NO UNIT CONVERSION. Coordinates arrive in SI metres as `Length` and leave
// the kernel as `1/m`, `m^3`, `W/K` and `J/K` through `fromSi`; there is no
// factor of a thousand in this file in either direction.

#include <bettercad/thermal/ThermalElement.hpp>

#include <cmath>
#include <format>
#include <limits>

namespace bettercad::thermal {
namespace {

/// A 3x3 matrix of SI values, row-major. Purpose-specific rather than a
/// general matrix type: see the header on why this module carries no linear
/// algebra library.
using Matrix3 = std::array<std::array<double, 3>, 3>;

/// The reference-element shape-function gradients, `grad_xi Ni`.
///
/// From FORMULATION.md: `N1 = 1 - xi - eta - zeta`, `N2 = xi`, `N3 = eta`,
/// `N4 = zeta`, so the first row is all minus one and the rest are the
/// identity. Constant, because a Tet4's shape functions are linear.
inline constexpr std::array<std::array<double, 3>, kTet4Nodes> kReferenceGradients{
    {{-1.0, -1.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

[[nodiscard]] bool isFinite(const Point3D& point) noexcept {
    return std::isfinite(point.x.si()) && std::isfinite(point.y.si()) &&
           std::isfinite(point.z.si());
}

/// `J[i][j] = d x_i / d xi_j`: the COLUMNS are the edge vectors from corner 0.
///
/// The convention is frozen in FORMULATION.md. Taking the transpose here would
/// silently transpose every gradient, which is why the gradient transformation
/// below uses `J^-T` and why the independent reference implementation in the
/// tests reaches the same gradients without forming a Jacobian at all.
[[nodiscard]] Matrix3 jacobian(const Tet4Corners& corners) noexcept {
    const Point3D& origin = corners[0];
    Matrix3 j{};
    for (std::size_t column = 0; column < 3; ++column) {
        const Point3D& corner = corners[column + 1];
        j[0][column] = corner.x.si() - origin.x.si();
        j[1][column] = corner.y.si() - origin.y.si();
        j[2][column] = corner.z.si() - origin.z.si();
    }
    return j;
}

/// `det J` as `e1 . (e2 x e3)` over the three COLUMNS of @p j.
///
/// GROUPED EXACTLY AS `meshing::signedVolume` GROUPS IT, for the reason
/// P17-ELEM-001 recorded: a cofactor expansion along the first row is the same
/// number algebraically and one ulp different on a skew tetrahedron, and the
/// two functions are predicates on the same boundary. A tetrahedron whose
/// volume rounds to within an ulp of zero must not be data-valid for
/// `MeshValidation` and inverted for this kernel. Exact agreement makes that
/// impossible rather than unlikely, and a test asserts it bit for bit.
///
/// `cross` is `{a.y b.z - a.z b.y, a.z b.x - a.x b.z, a.x b.y - a.y b.x}` and
/// `dot` sums left to right, both read from `src/meshing/Mesh.cpp`. No
/// `std::abs`: the sign is the orientation.
[[nodiscard]] double determinant(const Matrix3& j) noexcept {
    // e1 = column 0, e2 = column 1, e3 = column 2.
    const double crossX = j[1][1] * j[2][2] - j[2][1] * j[1][2];
    const double crossY = j[2][1] * j[0][2] - j[0][1] * j[2][2];
    const double crossZ = j[0][1] * j[1][2] - j[1][1] * j[0][2];
    return j[0][0] * crossX + j[1][0] * crossY + j[2][0] * crossZ;
}

/// `J^-1`, by the adjugate over @p det. The caller has already established
/// that @p det is finite and non-zero.
[[nodiscard]] Matrix3 inverse(const Matrix3& j, double det) noexcept {
    Matrix3 m{};
    m[0][0] = (j[1][1] * j[2][2] - j[1][2] * j[2][1]) / det;
    m[0][1] = (j[0][2] * j[2][1] - j[0][1] * j[2][2]) / det;
    m[0][2] = (j[0][1] * j[1][2] - j[0][2] * j[1][1]) / det;
    m[1][0] = (j[1][2] * j[2][0] - j[1][0] * j[2][2]) / det;
    m[1][1] = (j[0][0] * j[2][2] - j[0][2] * j[2][0]) / det;
    m[1][2] = (j[0][2] * j[1][0] - j[0][0] * j[1][2]) / det;
    m[2][0] = (j[1][0] * j[2][1] - j[1][1] * j[2][0]) / det;
    m[2][1] = (j[0][1] * j[2][0] - j[0][0] * j[2][1]) / det;
    m[2][2] = (j[0][0] * j[1][1] - j[0][1] * j[1][0]) / det;
    return m;
}

/// The Euclidean length of column @p column of @p j: an edge from corner 0.
[[nodiscard]] double edgeLength(const Matrix3& j, std::size_t column) noexcept {
    return std::sqrt(j[0][column] * j[0][column] + j[1][column] * j[1][column] +
                     j[2][column] * j[2][column]);
}

/// THE DEGENERACY BOUND: the largest `|det J|` that floating-point rounding
/// alone can produce from three edge vectors whose true triple product is
/// zero.
///
/// ```text
///     B = kDegeneracyNoiseFactor * eps * |e1| |e2| |e3|        [m^3]
/// ```
///
/// DERIVED, NOT CHOSEN. `det J` is evaluated as `e1 . fl(e2 x e3)`. Each cross
/// component is a difference of two products, so its rounding error is at
/// most `2u |e2||e3|` (Cauchy-Schwarz bounds the two product magnitudes by
/// `|e2||e3|`), with `u = 2^-53`; the dot product with `e1` adds at most
/// `3u |e1||e2||e3|` and carries the cross error through at most
/// `|e1| * 2u |e2||e3|`. The computed value therefore differs from the true
/// one by at most about `8u |e1||e2||e3|`, i.e. `4 eps |e1||e2||e3|` with
/// `eps = 2^-52`. A computed `|det J|` at or below that is indistinguishable
/// from zero: its SIGN is rounding noise, and an orientation read from it is
/// not evidence of anything. The factor 16 leaves a margin of four over that
/// bound, and the tests measure the noise actually produced by exactly
/// coplanar and coincident-corner tetrahedra against it.
///
/// SCALE-AWARE BY CONSTRUCTION, and not an absolute constant: the bound is
/// homogeneous of degree three in the coordinates, exactly as `det J` is, so
/// a tetrahedron and the same tetrahedron scaled by any `s` are classified
/// identically. It is a statement about the arithmetic, not about element
/// quality: a 1000:1 sliver has a relative volume of about 1e-3, twelve orders
/// above this bound, and what a thermal analysis accepts as a mesh is
/// P18-VALID-001's.
///
/// An overflow of the product (edges beyond ~1e102) gives an infinite bound
/// and so a degenerate verdict, which is correct: a volume that large
/// overflows anyway.
inline constexpr double kDegeneracyNoiseFactor = 16.0;

[[nodiscard]] double degeneracyBound(const Matrix3& j) noexcept {
    return kDegeneracyNoiseFactor * std::numeric_limits<double>::epsilon() * edgeLength(j, 0) *
           edgeLength(j, 1) * edgeLength(j, 2);
}

[[nodiscard]] bool isUsable(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

std::string_view toString(ElementProblem problem) noexcept {
    switch (problem) {
    case ElementProblem::NonFiniteCoordinate:
        return "non_finite_coordinate";
    case ElementProblem::DegenerateElement:
        return "degenerate_element";
    case ElementProblem::InvertedElement:
        return "inverted_element";
    case ElementProblem::NonFiniteResult:
        return "non_finite_result";
    case ElementProblem::InvalidConductivity:
        return "invalid_conductivity";
    case ElementProblem::InvalidDensity:
        return "invalid_density";
    case ElementProblem::InvalidSpecificHeat:
        return "invalid_specific_heat";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

std::array<InverseLength, 3> Tet4Geometry::gradient(std::size_t node) const noexcept {
    if (node >= kTet4Nodes) {
        return {};
    }
    return {InverseLength::fromSi(gradients_[node][0]), InverseLength::fromSi(gradients_[node][1]),
            InverseLength::fromSi(gradients_[node][2])};
}

InverseLength Tet4Geometry::g(std::size_t row, std::size_t column) const noexcept {
    if (row >= 3 || column >= kTet4Nodes) {
        return {};
    }
    // THE G CONVENTION, in one place: row is the axis, column is the corner,
    // so G(axis, node) = dN_node / d x_axis and grad T = G Te.
    return InverseLength::fromSi(gradients_[column][row]);
}

TemperatureGradient3 Tet4Geometry::gradientFrom(const Tet4Temperatures& temperatures) const noexcept {
    std::array<double, 3> gradient{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double sum = 0.0;
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            sum += gradients_[node][axis] * temperatures[node].si();
        }
        gradient[axis] = sum;
    }
    return TemperatureGradient3{.x = TemperatureGradient::fromSi(gradient[0]),
                                .y = TemperatureGradient::fromSi(gradient[1]),
                                .z = TemperatureGradient::fromSi(gradient[2])};
}

std::optional<ElementProblem> tet4GeometryProblem(const Tet4Corners& corners) noexcept {
    // P16's ORDER, WITH ONE REFINEMENT. A non-finite coordinate first, so no
    // arithmetic is done on one; then a non-finite determinant, which
    // MeshValidation classifies as degenerate rather than as its own kind
    // because "a comparison an infinity would answer 'greater'" must not be
    // reached; then DEGENERATE; then negative.
    //
    // THE REFINEMENT. P16 and P17 call a tetrahedron degenerate only when its
    // volume is EXACTLY zero. That is exact for integer-valued coordinates
    // and not for the general case: two corners at the same irrational-looking
    // position give edge vectors e1 == e2, and e1 . fl(e1 x e3) is not
    // exactly zero in floating point -- it is rounding noise of either sign,
    // so the exact-zero rule accepted a tetrahedron with two coincident
    // corners (found by this milestone's own coincident-node test) and its
    // sign decided whether it was "valid" or "inverted". The bound above is
    // the largest value rounding can produce from a true zero, so
    // |det| <= B is degenerate and only |det| > B has a sign worth reading.
    // Every tetrahedron whose |det| is above the bound is classified exactly
    // as P16 classifies it (same expression, same association, same sign).
    for (const Point3D& corner : corners) {
        if (!isFinite(corner)) {
            return ElementProblem::NonFiniteCoordinate;
        }
    }
    const Matrix3 j = jacobian(corners);
    const double det = determinant(j);
    if (!std::isfinite(det)) {
        return ElementProblem::DegenerateElement;
    }
    // `!(a > b)` rather than `a <= b`, so a NaN bound (inf * 0) is degenerate
    // too rather than comparing false both ways.
    if (!(std::abs(det) > degeneracyBound(j))) {
        return ElementProblem::DegenerateElement;
    }
    // P16's own volume test, kept: a determinant so small that /6 underflows
    // to zero encloses nothing representable, and P16 calls that degenerate.
    if (det / 6.0 == 0.0) {
        return ElementProblem::DegenerateElement;
    }
    if (det < 0.0) {
        return ElementProblem::InvertedElement;
    }
    return std::nullopt;
}

Result<Tet4Geometry> computeTet4Geometry(const Tet4Corners& corners) {
    if (const std::optional<ElementProblem> problem = tet4GeometryProblem(corners);
        problem.has_value()) {
        switch (*problem) {
        case ElementProblem::NonFiniteCoordinate:
            return makeError(ErrorCode::InvalidArgument,
                             "a corner coordinate is not finite, so the element has no geometry");
        case ElementProblem::DegenerateElement:
            return makeError(
                ErrorCode::FailedPrecondition,
                "the tetrahedron is degenerate: its signed volume is zero or not finite, so "
                "its four corners are coplanar and it encloses nothing");
        case ElementProblem::InvertedElement:
            return makeError(ErrorCode::FailedPrecondition,
                             "the tetrahedron is inverted: its signed volume is negative. Its "
                             "corners are not reordered here, because mesh correction belongs to "
                             "the mesher and a silent reorder would hide the defect");
        case ElementProblem::NonFiniteResult:
        case ElementProblem::InvalidConductivity:
        case ElementProblem::InvalidDensity:
        case ElementProblem::InvalidSpecificHeat:
            break;
        }
    }

    const Matrix3 j = jacobian(corners);
    const double det = determinant(j);
    const Matrix3 jInverse = inverse(j, det);

    Tet4Geometry geometry;
    geometry.determinant_ = Volume::fromSi(det);
    geometry.volume_ = Volume::fromSi(det / 6.0);

    // grad_x Ni = J^-T grad_xi Ni, as FORMULATION.md fixes. Written as the
    // full product for every corner rather than exploiting
    // grad N1 = -(grad N2 + grad N3 + grad N4): the identity is true, and
    // using it would make `sum grad Ni == 0` hold by construction and so turn
    // the partition-of-unity test into a tautology.
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        const std::array<double, 3>& reference = kReferenceGradients[node];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                // (J^-T r)_axis = sum_k (J^-1)_{k,axis} r_k
                sum += jInverse[k][axis] * reference[k];
            }
            geometry.gradients_[node][axis] = sum;
        }
    }

    for (const std::array<double, 3>& gradient : geometry.gradients_) {
        for (const double component : gradient) {
            if (!std::isfinite(component)) {
                return makeError(
                    ErrorCode::FailedPrecondition,
                    std::format("a shape-function gradient is not finite although the signed "
                                "volume {} is positive, so the element is too ill-conditioned "
                                "to evaluate",
                                geometry.volume_.si()));
            }
        }
    }
    return geometry;
}

Result<Tet4Corners> cornersOf(const meshing::Mesh& mesh, const meshing::Tetrahedron& tetrahedron) {
    Tet4Corners corners{};
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        const meshing::Node* node = mesh.findNode(tetrahedron.nodes[i]);
        if (node == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("element:{} names node:{}, which is not in the mesh",
                                         tetrahedron.id.value(), tetrahedron.nodes[i].value()));
        }
        corners[i] = node->position;
    }
    return corners;
}

// ---------------------------------------------------------------------------
// Conduction
// ---------------------------------------------------------------------------

ThermalConductance Tet4Conduction::operator()(std::size_t row, std::size_t column) const noexcept {
    if (row >= kTet4ThermalDofs || column >= kTet4ThermalDofs) {
        return {};
    }
    return ThermalConductance::fromSi(entries_[row * kTet4ThermalDofs + column]);
}

std::array<Power, kTet4Nodes>
Tet4Conduction::heatFlowFrom(const Tet4Temperatures& temperatures) const noexcept {
    std::array<Power, kTet4Nodes> flow{};
    for (std::size_t row = 0; row < kTet4ThermalDofs; ++row) {
        double sum = 0.0;
        for (std::size_t column = 0; column < kTet4ThermalDofs; ++column) {
            sum += entries_[row * kTet4ThermalDofs + column] * temperatures[column].si();
        }
        flow[row] = Power::fromSi(sum);
    }
    return flow;
}

std::optional<ElementProblem> conductivityProblem(ThermalConductivity conductivity) noexcept {
    if (!isUsable(conductivity.si())) {
        return ElementProblem::InvalidConductivity;
    }
    return std::nullopt;
}

Result<Tet4Conduction> computeTet4Conduction(const Tet4Geometry& geometry,
                                             ThermalConductivity conductivity) {
    // THE CONDUCTIVITY CONTRACT: finite and positive, or refused. No abs, no
    // epsilon. P15 refuses an unusable k at entry, so a resolved material
    // cannot reach here with one; a hand-built ThermalConductivity can.
    if (conductivityProblem(conductivity).has_value()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the thermal conductivity {} W/(m K) is not finite or not "
                                     "positive, so no conduction matrix can be formed from it",
                                     conductivity.si()));
    }

    // Ke_ij = k V (grad Ni . grad Nj). The scalar k V multiplies each dot
    // product once; dropping V is the mutation that leaves every dimension
    // wrong and every symmetry and null-mode property intact, which is why it
    // has its own probe and its own scale-law test. The dot product is
    // commutative term by term, so Ke is symmetric by construction.
    const double kv = conductivity.si() * geometry.volume().si();
    Tet4Conduction conduction;
    for (std::size_t row = 0; row < kTet4ThermalDofs; ++row) {
        const std::array<InverseLength, 3> gi = geometry.gradient(row);
        for (std::size_t column = 0; column < kTet4ThermalDofs; ++column) {
            const std::array<InverseLength, 3> gj = geometry.gradient(column);
            const double dot = gi[0].si() * gj[0].si() + gi[1].si() * gj[1].si() +
                               gi[2].si() * gj[2].si();
            const double entry = kv * dot;
            if (!std::isfinite(entry)) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("conduction entry ({}, {}) is not finite", row, column));
            }
            conduction.entries_[row * kTet4ThermalDofs + column] = entry;
        }
    }
    return conduction;
}

Result<Tet4Conduction> computeTet4Conduction(const Tet4Corners& corners,
                                             ThermalConductivity conductivity) {
    Result<Tet4Geometry> geometry = computeTet4Geometry(corners);
    if (!geometry.has_value()) {
        return std::unexpected(geometry.error());
    }
    return computeTet4Conduction(*geometry, conductivity);
}

// ---------------------------------------------------------------------------
// Capacity
// ---------------------------------------------------------------------------

HeatCapacity Tet4Capacity::operator()(std::size_t row, std::size_t column) const noexcept {
    if (row >= kTet4ThermalDofs || column >= kTet4ThermalDofs) {
        return {};
    }
    return HeatCapacity::fromSi(entries_[row * kTet4ThermalDofs + column]);
}

HeatCapacity Tet4Capacity::total() const noexcept {
    double sum = 0.0;
    for (const double entry : entries_) {
        sum += entry;
    }
    return HeatCapacity::fromSi(sum);
}

std::optional<ElementProblem> capacityProblem(const ThermalCapacity& capacity) noexcept {
    if (!isUsable(capacity.density.si())) {
        return ElementProblem::InvalidDensity;
    }
    if (!isUsable(capacity.specificHeatCapacity.si())) {
        return ElementProblem::InvalidSpecificHeat;
    }
    return std::nullopt;
}

Result<Tet4Capacity> computeTet4Capacity(const Tet4Geometry& geometry, const ThermalCapacity& capacity) {
    // THE CAPACITY CONTRACT: rho and cp each finite and positive, named
    // separately, or refused. No abs, no substitution.
    if (const std::optional<ElementProblem> problem = capacityProblem(capacity); problem.has_value()) {
        if (*problem == ElementProblem::InvalidDensity) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the density {} kg/m^3 is not finite or not positive, so no "
                                         "capacity matrix can be formed from it",
                                         capacity.density.si()));
        }
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the specific heat capacity {} J/(kg K) is not finite or not "
                                     "positive, so no capacity matrix can be formed from it",
                                     capacity.specificHeatCapacity.si()));
    }

    // THE CONSISTENT MATRIX, IN CLOSED FORM. rho cp V / 20 is the off-diagonal
    // entry (integral Ni Nj dV = V/20) and twice it is the diagonal (integral
    // Ni^2 dV = V/10). Not integrated: Ni Nj is quadratic, and a one-point
    // centroid rule would put rho cp V / 16 in every entry -- the right row
    // sums, the wrong matrix, rank one. Not lumped: there is no diagonal
    // variant. The product is formed as rho cp V, in that order, once.
    const double rhoCpV = capacity.density.si() * capacity.specificHeatCapacity.si() *
                          geometry.volume().si();
    if (!std::isfinite(rhoCpV)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("rho cp V is not finite ({} kg/m^3 x {} J/(kg K) x {} m^3), so "
                                     "no capacity matrix can be formed",
                                     capacity.density.si(), capacity.specificHeatCapacity.si(),
                                     geometry.volume().si()));
    }
    const double offDiagonal = rhoCpV / 20.0;
    const double diagonal = 2.0 * offDiagonal;

    Tet4Capacity result;
    for (std::size_t row = 0; row < kTet4ThermalDofs; ++row) {
        for (std::size_t column = 0; column < kTet4ThermalDofs; ++column) {
            const double entry = row == column ? diagonal : offDiagonal;
            if (!std::isfinite(entry)) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("capacity entry ({}, {}) is not finite", row, column));
            }
            result.entries_[row * kTet4ThermalDofs + column] = entry;
        }
    }
    return result;
}

Result<Tet4Capacity> computeTet4Capacity(const Tet4Corners& corners, const ThermalCapacity& capacity) {
    Result<Tet4Geometry> geometry = computeTet4Geometry(corners);
    if (!geometry.has_value()) {
        return std::unexpected(geometry.error());
    }
    return computeTet4Capacity(*geometry, capacity);
}

} // namespace bettercad::thermal
