// The linear-elastic Tet4 element kernel (P17-ELEM-001).
//
// EVERY FORMULA HERE IS FIXED BY
// docs/verification/P17-ELEM-001/TET4_DERIVATION.md, which was written first.
// The Jacobian's columns are the three edge vectors from node 0; physical
// gradients are `J^-T grad_xi N`; `det J = (p2-p1) . ((p3-p1) x (p4-p1))`,
// which is the same expression `meshing::signedVolume` computes; `V = det J/6`
// with NO absolute value anywhere; `B` rows are `exx eyy ezz gxy gyz gzx` with
// ENGINEERING shear; `D` carries `mu` on the shear diagonal, not `2 mu`; and
// `Ke = V B^T D B` exactly, because both factors are constant over a Tet4.
//
// NOTHING IS SYMMETRISED AND NOTHING IS REGULARISED. `B^T D B` is symmetric
// because `D` is, so a `0.5 * (K + K^T)` here would hide a formula error
// rather than fix a numerical one; and a free Tet4's `Ke` is meant to be
// singular, with six rigid-body modes, so adding anything to the diagonal
// would delete the physics.
//
// NO HIDDEN STATE. Every function is a pure computation over its arguments:
// no `static` storage, no cache, no `mutable`, no `thread_local`. That makes
// determinism structural and leaves future parallel assembly open without
// anything to revisit here.

#include <bettercad/structural/Tet4Element.hpp>

#include <bettercad/meshing/Mesh.hpp>

#include <cmath>
#include <format>

namespace bettercad::structural {
namespace {

/// A 3x3 matrix of SI values, row-major. Purpose-specific rather than a
/// general matrix type: see the header on why this module carries no linear
/// algebra library.
using Matrix3 = std::array<std::array<double, 3>, 3>;

/// The reference-element shape-function gradients, `grad_xi Ni`.
///
/// From TET4_DERIVATION.md: `N1 = 1 - xi - eta - zeta`, `N2 = xi`,
/// `N3 = eta`, `N4 = zeta`, so the first row is all minus one and the rest are
/// the identity. Constant, because a Tet4's shape functions are linear.
inline constexpr std::array<std::array<double, 3>, kTet4Nodes> kReferenceGradients{
    {{-1.0, -1.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

[[nodiscard]] bool isFinite(const Point3D& point) noexcept {
    return std::isfinite(point.x.si()) && std::isfinite(point.y.si()) &&
           std::isfinite(point.z.si());
}

/// `J[i][j] = d x_i / d xi_j`: the COLUMNS are the edge vectors from node 0.
///
/// The convention is frozen in TET4_DERIVATION.md. Taking the transpose here
/// would silently transpose every gradient, which is why the gradient
/// transformation below uses `J^-T` and why the independent reference
/// implementation reaches the same gradients by a different route.
[[nodiscard]] Matrix3 jacobian(const std::array<Point3D, kTet4Nodes>& nodes) noexcept {
    const Point3D& origin = nodes[0];
    Matrix3 j{};
    for (std::size_t column = 0; column < 3; ++column) {
        const Point3D& corner = nodes[column + 1];
        j[0][column] = corner.x.si() - origin.x.si();
        j[1][column] = corner.y.si() - origin.y.si();
        j[2][column] = corner.z.si() - origin.z.si();
    }
    return j;
}

/// `det J` as `e1 . (e2 x e3)` over the three COLUMNS of @p j.
///
/// GROUPED EXACTLY AS `meshing::signedVolume` GROUPS IT, and that is the whole
/// point of writing it this way. The first draft expanded cofactors along the
/// first row -- algebraically the same number, and **one ulp different** on a
/// skew tetrahedron, because floating-point addition is not associative. The
/// test that asserted the two agree bit for bit caught it.
///
/// Matching the association rather than relaxing the test is the safer choice,
/// because the two functions are predicates on the same boundary: a
/// tetrahedron whose volume rounds to within an ulp of zero must not be
/// data-valid for `MeshValidation` and inverted for this kernel, or a
/// qualified mesh would contain an element the solver refuses. Exact agreement
/// makes that impossible rather than unlikely.
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

/// `D` as a dense 6x6 of SI values, from the two Lame parameters.
///
/// `mu` on the shear diagonal. See the header: with engineering shear the
/// factor of two is already in `gamma`.
[[nodiscard]] std::array<std::array<double, kVoigtComponents>, kVoigtComponents>
dense(double lame, double shear) noexcept {
    std::array<std::array<double, kVoigtComponents>, kVoigtComponents> d{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            d[row][column] = lame + (row == column ? 2.0 * shear : 0.0);
        }
    }
    for (std::size_t row = 3; row < kVoigtComponents; ++row) {
        d[row][row] = shear;
    }
    return d;
}

/// The local displacement vector of @p displacements, in `localDofIndex`
/// order. SI metres.
[[nodiscard]] std::array<double, kTet4Dofs>
flatten(const std::array<Translation3D, kTet4Nodes>& displacements) noexcept {
    std::array<double, kTet4Dofs> u{};
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        u[localDofIndex(node, DofComponent::Ux)] = displacements[node].x.si();
        u[localDofIndex(node, DofComponent::Uy)] = displacements[node].y.si();
        u[localDofIndex(node, DofComponent::Uz)] = displacements[node].z.si();
    }
    return u;
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
    case ElementProblem::InvalidMaterial:
        return "invalid_material";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Kinematics
// ---------------------------------------------------------------------------

std::array<InverseLength, 3> Tet4Kinematics::gradient(std::size_t node) const noexcept {
    if (node >= kTet4Nodes) {
        return {};
    }
    return {InverseLength::fromSi(gradients_[node][0]),
            InverseLength::fromSi(gradients_[node][1]),
            InverseLength::fromSi(gradients_[node][2])};
}

InverseLength Tet4Kinematics::b(std::size_t row, std::size_t column) const noexcept {
    if (row >= kVoigtComponents || column >= kTet4Dofs) {
        return {};
    }
    const std::size_t node = column / kDofsPerNode;
    const std::size_t axis = column % kDofsPerNode;
    const std::array<double, 3>& g = gradients_[node];

    // THE B CONVENTION, in one place. Rows are exx eyy ezz gxy gyz gzx;
    // `axis` is 0 for Ux, 1 for Uy, 2 for Uz, which is kDofComponents' order
    // and so localDofIndex's. Each shear row pairs the two gradients the
    // engineering shear definition pairs:
    //     gxy = dux/dy + duy/dx  ->  Ux gets dN/dy, Uy gets dN/dx
    //     gyz = duy/dz + duz/dy  ->  Uy gets dN/dz, Uz gets dN/dy
    //     gzx = duz/dx + dux/dz  ->  Uz gets dN/dx, Ux gets dN/dz
    switch (static_cast<TensorComponent>(row)) {
    case TensorComponent::XX:
        return axis == 0 ? InverseLength::fromSi(g[0]) : InverseLength{};
    case TensorComponent::YY:
        return axis == 1 ? InverseLength::fromSi(g[1]) : InverseLength{};
    case TensorComponent::ZZ:
        return axis == 2 ? InverseLength::fromSi(g[2]) : InverseLength{};
    case TensorComponent::XY:
        if (axis == 0) {
            return InverseLength::fromSi(g[1]);
        }
        return axis == 1 ? InverseLength::fromSi(g[0]) : InverseLength{};
    case TensorComponent::YZ:
        if (axis == 1) {
            return InverseLength::fromSi(g[2]);
        }
        return axis == 2 ? InverseLength::fromSi(g[1]) : InverseLength{};
    case TensorComponent::ZX:
        if (axis == 2) {
            return InverseLength::fromSi(g[0]);
        }
        return axis == 0 ? InverseLength::fromSi(g[2]) : InverseLength{};
    }
    return {};
}

Strain6
Tet4Kinematics::strainFrom(const std::array<Translation3D, kTet4Nodes>& displacements) const noexcept {
    const std::array<double, kTet4Dofs> u = flatten(displacements);
    std::array<double, kVoigtComponents> eps{};
    for (std::size_t row = 0; row < kVoigtComponents; ++row) {
        double sum = 0.0;
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            sum += b(row, column).si() * u[column];
        }
        eps[row] = sum;
    }
    return Strain6{.xx = eps[0],
                   .yy = eps[1],
                   .zz = eps[2],
                   .gammaXy = eps[3],
                   .gammaYz = eps[4],
                   .gammaZx = eps[5]};
}

std::optional<ElementProblem>
tet4GeometryProblem(const std::array<Point3D, kTet4Nodes>& nodes) noexcept {
    // P16's RULE, IN P16'S ORDER. A non-finite coordinate first, so no
    // arithmetic is done on one; then a non-finite determinant, which
    // MeshValidation classifies as degenerate rather than as its own kind
    // because "a comparison an infinity would answer 'greater'" must not be
    // reached; then exactly zero; then negative.
    for (const Point3D& node : nodes) {
        if (!isFinite(node)) {
            return ElementProblem::NonFiniteCoordinate;
        }
    }
    const double det = determinant(jacobian(nodes));
    if (!std::isfinite(det)) {
        return ElementProblem::DegenerateElement;
    }
    if (det == 0.0) {
        return ElementProblem::DegenerateElement;
    }
    if (det < 0.0) {
        return ElementProblem::InvertedElement;
    }
    return std::nullopt;
}

Result<Tet4Kinematics> computeTet4Kinematics(const std::array<Point3D, kTet4Nodes>& nodes) {
    if (const std::optional<ElementProblem> problem = tet4GeometryProblem(nodes);
        problem.has_value()) {
        switch (*problem) {
        case ElementProblem::NonFiniteCoordinate:
            return makeError(ErrorCode::InvalidArgument,
                             "a node coordinate is not finite, so the element has no geometry");
        case ElementProblem::DegenerateElement:
            return makeError(
                ErrorCode::FailedPrecondition,
                "the tetrahedron is degenerate: its signed volume is zero or not finite, so "
                "its four nodes are coplanar and it encloses nothing");
        case ElementProblem::InvertedElement:
            return makeError(ErrorCode::FailedPrecondition,
                             "the tetrahedron is inverted: its signed volume is negative. Its "
                             "nodes are not reordered here, because mesh correction belongs to "
                             "the mesher and a silent reorder would hide the defect");
        case ElementProblem::NonFiniteResult:
        case ElementProblem::InvalidMaterial:
            break;
        }
    }

    const Matrix3 j = jacobian(nodes);
    const double det = determinant(j);
    const Matrix3 jInverse = inverse(j, det);

    Tet4Kinematics kinematics;
    kinematics.determinant_ = Volume::fromSi(det);
    kinematics.volume_ = Volume::fromSi(det / 6.0);

    // grad_x Ni = J^-T grad_xi Ni, as derived in TET4_DERIVATION.md. Written
    // as the full product for every node rather than exploiting
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
            kinematics.gradients_[node][axis] = sum;
        }
    }

    for (const std::array<double, 3>& gradient : kinematics.gradients_) {
        for (const double component : gradient) {
            if (!std::isfinite(component)) {
                return makeError(
                    ErrorCode::FailedPrecondition,
                    std::format("a shape-function gradient is not finite although the signed "
                                "volume {} is positive, so the element is too ill-conditioned "
                                "to evaluate",
                                kinematics.volume_.si()));
            }
        }
    }
    return kinematics;
}

// ---------------------------------------------------------------------------
// Constitutive
// ---------------------------------------------------------------------------

Stress ElasticityMatrix::d(std::size_t row, std::size_t column) const noexcept {
    if (row >= kVoigtComponents || column >= kVoigtComponents) {
        return {};
    }
    return Stress::fromSi(dense(lame_.si(), shear_.si())[row][column]);
}

Stress6 ElasticityMatrix::stressFrom(const Strain6& strain) const noexcept {
    const std::array<std::array<double, kVoigtComponents>, kVoigtComponents> matrix =
        dense(lame_.si(), shear_.si());
    const std::array<double, kVoigtComponents> eps{strain.xx,      strain.yy,      strain.zz,
                                                   strain.gammaXy, strain.gammaYz, strain.gammaZx};
    std::array<double, kVoigtComponents> sigma{};
    for (std::size_t row = 0; row < kVoigtComponents; ++row) {
        double sum = 0.0;
        for (std::size_t column = 0; column < kVoigtComponents; ++column) {
            sum += matrix[row][column] * eps[column];
        }
        sigma[row] = sum;
    }
    return Stress6{.xx = Stress::fromSi(sigma[0]),
                   .yy = Stress::fromSi(sigma[1]),
                   .zz = Stress::fromSi(sigma[2]),
                   .xy = Stress::fromSi(sigma[3]),
                   .yz = Stress::fromSi(sigma[4]),
                   .zx = Stress::fromSi(sigma[5])};
}

Result<ElasticityMatrix> isotropicElasticity(const materials::LinearElasticConstants& constants) {
    const double modulus = constants.youngsModulus.si();
    const double ratio = constants.poissonRatio.value();

    // lambda FROM THE FROZEN FORMULA. mu is NOT recomputed: P15's
    // shearModulus already IS E/(2(1+nu)), derived with exactly this formula,
    // so taking its value means there is one definition in the tree. The
    // identity lambda = K - 2mu/3 cross-checks the two, and is a test rather
    // than a second code path.
    const double lame = modulus * ratio / ((1.0 + ratio) * (1.0 - 2.0 * ratio));
    const double shear = constants.shearModulus.si();

    // FINITENESS ONLY. The admissible range is P15's: createMaterial and
    // setMaterialMechanical both refuse an unusable E or nu at entry
    // (P17-MAT-001), so a material resolved through StructuralMaterial cannot
    // be out of range, and re-deriving that check here is the duplication
    // ADR-028 forbids. What a hand-filled LinearElasticConstants can still do
    // is make lambda or mu non-finite -- nu exactly -1 or 0.5, or a NaN input
    // -- and a NaN must not reach Ke silently.
    if (!std::isfinite(lame) || !std::isfinite(shear)) {
        return makeError(
            ErrorCode::InvalidArgument,
            std::format("the elastic constants do not yield finite Lame parameters: lambda is "
                        "{} and mu is {} for E = {} Pa and nu = {}",
                        lame, shear, modulus, ratio));
    }

    ElasticityMatrix elasticity;
    elasticity.lame_ = ElasticModulus::fromSi(lame);
    elasticity.shear_ = ElasticModulus::fromSi(shear);
    return elasticity;
}

// ---------------------------------------------------------------------------
// Stiffness
// ---------------------------------------------------------------------------

Stiffness Tet4Stiffness::operator()(std::size_t row, std::size_t column) const noexcept {
    if (row >= kTet4Dofs || column >= kTet4Dofs) {
        return {};
    }
    return Stiffness::fromSi(entries_[row * kTet4Dofs + column]);
}

std::array<Force3D, kTet4Nodes>
Tet4Stiffness::forceFrom(const std::array<Translation3D, kTet4Nodes>& displacements) const noexcept {
    const std::array<double, kTet4Dofs> u = flatten(displacements);
    std::array<double, kTet4Dofs> f{};
    for (std::size_t row = 0; row < kTet4Dofs; ++row) {
        double sum = 0.0;
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            sum += entries_[row * kTet4Dofs + column] * u[column];
        }
        f[row] = sum;
    }
    std::array<Force3D, kTet4Nodes> forces{};
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        forces[node] = Force3D{Force::fromSi(f[localDofIndex(node, DofComponent::Ux)]),
                               Force::fromSi(f[localDofIndex(node, DofComponent::Uy)]),
                               Force::fromSi(f[localDofIndex(node, DofComponent::Uz)])};
    }
    return forces;
}

Energy
Tet4Stiffness::energyFrom(const std::array<Translation3D, kTet4Nodes>& displacements) const noexcept {
    const std::array<double, kTet4Dofs> u = flatten(displacements);
    double energy = 0.0;
    for (std::size_t row = 0; row < kTet4Dofs; ++row) {
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            energy += u[row] * entries_[row * kTet4Dofs + column] * u[column];
        }
    }
    return Energy::fromSi(0.5 * energy);
}

Result<Tet4Stiffness> computeTet4Stiffness(const Tet4Kinematics& kinematics,
                                           const ElasticityMatrix& elasticity) {
    // B and D as dense SI arrays, read once through the public accessors so
    // there is no second copy of either convention in this function.
    std::array<std::array<double, kTet4Dofs>, kVoigtComponents> matrixB{};
    for (std::size_t row = 0; row < kVoigtComponents; ++row) {
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            matrixB[row][column] = kinematics.b(row, column).si();
        }
    }
    std::array<std::array<double, kVoigtComponents>, kVoigtComponents> matrixD{};
    for (std::size_t row = 0; row < kVoigtComponents; ++row) {
        for (std::size_t column = 0; column < kVoigtComponents; ++column) {
            matrixD[row][column] = elasticity.d(row, column).si();
        }
    }

    // D B, then B^T (D B), then scale by V. Forming the intermediate is
    // O(6 * 6 * 12 + 6 * 12 * 12) instead of O(12 * 12 * 6 * 6), and more to
    // the point it keeps the expression recognisable as V B^T D B.
    std::array<std::array<double, kTet4Dofs>, kVoigtComponents> db{};
    for (std::size_t row = 0; row < kVoigtComponents; ++row) {
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kVoigtComponents; ++k) {
                sum += matrixD[row][k] * matrixB[k][column];
            }
            db[row][column] = sum;
        }
    }

    const double volume = kinematics.volume().si();
    Tet4Stiffness stiffness;
    for (std::size_t row = 0; row < kTet4Dofs; ++row) {
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kVoigtComponents; ++k) {
                sum += matrixB[k][row] * db[k][column];
            }
            // The volume multiplies the triple product once. Dropping it is
            // the mutation that leaves every dimension wrong and every
            // symmetry and rigid-body property intact, which is why it has its
            // own probe and its own scale-law test.
            const double entry = volume * sum;
            if (!std::isfinite(entry)) {
                return makeError(
                    ErrorCode::FailedPrecondition,
                    std::format("stiffness entry ({}, {}) is not finite", row, column));
            }
            stiffness.entries_[row * kTet4Dofs + column] = entry;
        }
    }
    return stiffness;
}

Result<Tet4Stiffness> computeTet4Stiffness(const std::array<Point3D, kTet4Nodes>& nodes,
                                           const materials::LinearElasticConstants& constants) {
    Result<Tet4Kinematics> kinematics = computeTet4Kinematics(nodes);
    if (!kinematics.has_value()) {
        return std::unexpected(kinematics.error());
    }
    Result<ElasticityMatrix> elasticity = isotropicElasticity(constants);
    if (!elasticity.has_value()) {
        return std::unexpected(elasticity.error());
    }
    return computeTet4Stiffness(*kinematics, *elasticity);
}

} // namespace bettercad::structural
