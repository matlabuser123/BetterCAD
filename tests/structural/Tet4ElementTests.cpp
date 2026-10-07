// P17-ELEM-001: the linear-elastic Tet4 element kernel.
//
// THE SPECIFICATION IS docs/verification/P17-ELEM-001/TET4_DERIVATION.md,
// written before the implementation. These tests check the code against that
// document; where a value is quoted below it was derived by hand or by the
// independent reference, never read out of production.
//
// THE INDEPENDENT REFERENCE TAKES A DIFFERENT ROUTE, which is the point of it.
// Production forms a 3x3 Jacobian, takes its determinant by cofactor expansion
// and maps reference gradients through `J^-T`. The reference below inverts the
// 4x4 matrix `[1 x y z]` with Eigen and reads the shape-function coefficients
// straight out of it, so it never forms a Jacobian and never transposes one.
// It computes `lambda` and `mu` from `E` and `nu` itself rather than taking
// P15's derived shear modulus, and it multiplies with Eigen rather than with
// hand-written loops. It calls NO production helper -- that is checked by
// construction: the only production symbols this file's reference touches are
// the `Point3D` members of its argument.
//
// Eigen is used here and NOT in production. `src/structural/CMakeLists.txt`
// records that admitting Eigen to the structural library is P17-SOLVE-001's
// decision and carries an unresolved licence question; the test binary already
// links it, and eigenvalues, rank and a 4x4 inverse are exactly what a test
// needs and a 12x12 triple product does not.

#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/Tet4Element.hpp>

#include <Eigen/Dense>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::DofComponent;
using structural::ElasticityMatrix;
using structural::ElementProblem;
using structural::kTet4Dofs;
using structural::kTet4Nodes;
using structural::kVoigtComponents;
using structural::localDofIndex;
using structural::Strain6;
using structural::Stress6;
using structural::Tet4Kinematics;
using structural::Tet4Stiffness;

namespace {

using Nodes = std::array<Point3D, kTet4Nodes>;
using Displacements = std::array<Translation3D, kTet4Nodes>;

[[nodiscard]] Point3D at(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

[[nodiscard]] Translation3D move(double x, double y, double z) {
    return Translation3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

/// The reference tetrahedron of TET4_DERIVATION.md, in metres.
[[nodiscard]] Nodes unitTet() {
    return {at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0, 0, 1)};
}

/// A skew tetrahedron: no edge is axis-aligned and no two are orthogonal, so
/// an implementation that happens to work only for the reference element
/// cannot pass. Deliberately not derived from `unitTet()`.
[[nodiscard]] Nodes skewTet() {
    return {at(0.31, -0.17, 0.44), at(1.27, 0.09, 0.21), at(0.05, 0.93, -0.38),
            at(-0.22, 0.14, 1.36)};
}

/// Valid but badly shaped: 1000:1 flat. Positive volume, so P16 calls it
/// data-valid and so must this kernel -- `invalid` is not `poor quality`.
[[nodiscard]] Nodes thinTet() {
    return {at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0.3, 0.3, 1.0e-3)};
}

[[nodiscard]] materials::LinearElasticConstants constants(double youngs, double poisson) {
    materials::LinearElasticConstants c;
    c.youngsModulus = ElasticModulus::fromSi(youngs);
    c.poissonRatio = PoissonRatio::of(poisson);
    // P15 derives both of these with these exact formulas; the fixture mirrors
    // them so a test material is indistinguishable from a resolved one.
    c.shearModulus = ElasticModulus::fromSi(youngs / (2.0 * (1.0 + poisson)));
    c.bulkModulus = ElasticModulus::fromSi(youngs / (3.0 * (1.0 - 2.0 * poisson)));
    return c;
}

// -----------------------------------------------------------------------
// The independent reference implementation
// -----------------------------------------------------------------------

/// `B`, `D`, `Ke` and the volume of one Tet4, by a route that shares no code
/// with production.
struct Reference {
    double volume = 0.0;
    /// Row `i` is `grad Ni`.
    Eigen::Matrix<double, 4, 3> gradients = Eigen::Matrix<double, 4, 3>::Zero();
    Eigen::Matrix<double, 6, 12> b = Eigen::Matrix<double, 6, 12>::Zero();
    Eigen::Matrix<double, 6, 6> d = Eigen::Matrix<double, 6, 6>::Zero();
    Eigen::Matrix<double, 12, 12> ke = Eigen::Matrix<double, 12, 12>::Zero();
    double lame = 0.0;
    double shear = 0.0;
};

/// THE DIFFERENT ROUTE. `Ni(x,y,z) = ai + bi x + ci y + di z` with
/// `Ni(node j) = delta_ij`, so with `A` the 4x4 whose rows are `[1 xj yj zj]`,
/// column `i` of `A^-1` holds node `i`'s coefficients and rows 1..3 of `A^-1`
/// hold the gradients directly. No Jacobian is formed, so a transposition in
/// production cannot be mirrored here. `V = det(A)/6`, by a 4x4 determinant
/// rather than a 3x3 cofactor expansion.
[[nodiscard]] Reference reference(const Nodes& p, double youngs, double poisson) {
    Eigen::Matrix4d a;
    for (int i = 0; i < 4; ++i) {
        a(i, 0) = 1.0;
        a(i, 1) = p[static_cast<std::size_t>(i)].x.si();
        a(i, 2) = p[static_cast<std::size_t>(i)].y.si();
        a(i, 3) = p[static_cast<std::size_t>(i)].z.si();
    }

    Reference out;
    out.volume = a.determinant() / 6.0;
    const Eigen::Matrix4d coefficients = a.inverse();
    for (int node = 0; node < 4; ++node) {
        for (int axis = 0; axis < 3; ++axis) {
            out.gradients(node, axis) = coefficients(axis + 1, node);
        }
    }

    // B, from the block of TET4_DERIVATION.md, written out here rather than
    // looped so the six rows are visible and a permuted row is a visible edit.
    for (int node = 0; node < 4; ++node) {
        const double bi = out.gradients(node, 0);
        const double ci = out.gradients(node, 1);
        const double di = out.gradients(node, 2);
        const int ux = 3 * node + 0;
        const int uy = 3 * node + 1;
        const int uz = 3 * node + 2;
        out.b(0, ux) = bi;                      // exx
        out.b(1, uy) = ci;                      // eyy
        out.b(2, uz) = di;                      // ezz
        out.b(3, ux) = ci;  out.b(3, uy) = bi;  // gxy
        out.b(4, uy) = di;  out.b(4, uz) = ci;  // gyz
        out.b(5, uz) = bi;  out.b(5, ux) = di;  // gzx
    }

    // lambda and mu computed here from E and nu -- NOT taken from P15's
    // derived shearModulus, so the reference does not inherit production's
    // choice to reuse it.
    out.lame = youngs * poisson / ((1.0 + poisson) * (1.0 - 2.0 * poisson));
    out.shear = youngs / (2.0 * (1.0 + poisson));
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out.d(i, j) = out.lame + (i == j ? 2.0 * out.shear : 0.0);
        }
    }
    for (int i = 3; i < 6; ++i) {
        out.d(i, i) = out.shear;
    }

    out.ke = out.volume * out.b.transpose() * out.d * out.b;
    return out;
}

// -----------------------------------------------------------------------
// Production readers, for comparison
// -----------------------------------------------------------------------

[[nodiscard]] Tet4Kinematics kinematicsOf(const Nodes& nodes) {
    Result<Tet4Kinematics> k = structural::computeTet4Kinematics(nodes);
    INFO((k.has_value() ? std::string{} : k.error().message));
    REQUIRE(k.has_value());
    return *k;
}

[[nodiscard]] ElasticityMatrix elasticityOf(double youngs, double poisson) {
    Result<ElasticityMatrix> d = structural::isotropicElasticity(constants(youngs, poisson));
    INFO((d.has_value() ? std::string{} : d.error().message));
    REQUIRE(d.has_value());
    return *d;
}

[[nodiscard]] Tet4Stiffness stiffnessOf(const Nodes& nodes, double youngs, double poisson) {
    Result<Tet4Stiffness> k =
        structural::computeTet4Stiffness(nodes, constants(youngs, poisson));
    INFO((k.has_value() ? std::string{} : k.error().message));
    REQUIRE(k.has_value());
    return *k;
}

[[nodiscard]] Eigen::Matrix<double, 6, 12> productionB(const Tet4Kinematics& k) {
    Eigen::Matrix<double, 6, 12> b;
    for (std::size_t row = 0; row < kVoigtComponents; ++row) {
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            b(static_cast<int>(row), static_cast<int>(column)) = k.b(row, column).si();
        }
    }
    return b;
}

[[nodiscard]] Eigen::Matrix<double, 6, 6> productionD(const ElasticityMatrix& e) {
    Eigen::Matrix<double, 6, 6> d;
    for (std::size_t row = 0; row < kVoigtComponents; ++row) {
        for (std::size_t column = 0; column < kVoigtComponents; ++column) {
            d(static_cast<int>(row), static_cast<int>(column)) = e.d(row, column).si();
        }
    }
    return d;
}

[[nodiscard]] Eigen::Matrix<double, 12, 12> productionKe(const Tet4Stiffness& k) {
    Eigen::Matrix<double, 12, 12> ke;
    for (std::size_t row = 0; row < kTet4Dofs; ++row) {
        for (std::size_t column = 0; column < kTet4Dofs; ++column) {
            ke(static_cast<int>(row), static_cast<int>(column)) = k(row, column).si();
        }
    }
    return ke;
}

/// The six rigid-body modes of a free tetrahedron, as local DOF vectors:
/// three unit translations and three infinitesimal rotations `u = omega x x`
/// about the element's own centroid.
[[nodiscard]] std::array<Eigen::Matrix<double, 12, 1>, 6> rigidModes(const Nodes& p) {
    Eigen::Vector3d centroid = Eigen::Vector3d::Zero();
    for (const Point3D& node : p) {
        centroid += Eigen::Vector3d(node.x.si(), node.y.si(), node.z.si());
    }
    centroid /= 4.0;

    std::array<Eigen::Matrix<double, 12, 1>, 6> modes{};
    for (auto& mode : modes) {
        mode.setZero();
    }
    for (int node = 0; node < 4; ++node) {
        const Eigen::Vector3d x(p[static_cast<std::size_t>(node)].x.si(),
                                p[static_cast<std::size_t>(node)].y.si(),
                                p[static_cast<std::size_t>(node)].z.si());
        const Eigen::Vector3d r = x - centroid;
        for (int axis = 0; axis < 3; ++axis) {
            modes[static_cast<std::size_t>(axis)](3 * node + axis) = 1.0;
            Eigen::Vector3d omega = Eigen::Vector3d::Zero();
            omega(axis) = 1.0;
            const Eigen::Vector3d u = omega.cross(r);
            for (int component = 0; component < 3; ++component) {
                modes[static_cast<std::size_t>(axis + 3)](3 * node + component) = u(component);
            }
        }
    }
    return modes;
}

[[nodiscard]] Displacements toDisplacements(const Eigen::Matrix<double, 12, 1>& u) {
    Displacements d{};
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        d[node] = move(u(static_cast<int>(3 * node + 0)), u(static_cast<int>(3 * node + 1)),
                       u(static_cast<int>(3 * node + 2)));
    }
    return d;
}

/// Nodal displacements sampled from an affine field, which is the only field a
/// Tet4 represents exactly.
[[nodiscard]] Displacements affine(const Nodes& p, const std::array<double, 12>& c) {
    Displacements d{};
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        const double x = p[node].x.si();
        const double y = p[node].y.si();
        const double z = p[node].z.si();
        d[node] = move(c[0] + c[1] * x + c[2] * y + c[3] * z,
                       c[4] + c[5] * x + c[6] * y + c[7] * z,
                       c[8] + c[9] * x + c[10] * y + c[11] * z);
    }
    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// Shape functions and gradients
// ---------------------------------------------------------------------------

TEST_CASE("Tet4Element_ReferenceGradientsAreExactForTheUnitTetrahedron",
          "[structural][elem]") {
    // TET4_DERIVATION.md's analytical values, written out by hand. For the
    // reference element J = I, so the physical gradients ARE the reference
    // gradients and every entry is an exact small integer -- no tolerance is
    // needed and none is used.
    const Tet4Kinematics k = kinematicsOf(unitTet());

    CHECK(k.determinant().si() == 1.0);
    CHECK(k.volume().si() == 1.0 / 6.0);

    const std::array<std::array<double, 3>, 4> expected{
        {{-1.0, -1.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        const std::array<structural::InverseLength, 3> g = k.gradient(node);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            INFO("node " << node << " axis " << axis);
            CHECK(g[axis].si() == expected[node][axis]);
        }
    }
    // Out of range gives zero, not a read past the end.
    for (const structural::InverseLength component : k.gradient(kTet4Nodes)) {
        CHECK(component.si() == 0.0);
    }
}

TEST_CASE("Tet4Element_ShapeGradientsSumToZeroOnEveryFixture", "[structural][elem]") {
    // Because `sum Ni = 1` everywhere, `sum grad Ni = 0`. A geometry-gradient
    // check that needs no reference values, and a real one: production does
    // NOT compute grad N1 as minus the sum of the others, so this holds only
    // if the inverse is right.
    struct Case {
        const char* label;
        Nodes nodes;
    };
    const std::array<Case, 4> cases{{{"unit", unitTet()},
                                     {"skew", skewTet()},
                                     {"thin", thinTet()},
                                     {"far", {at(1e5, -2e5, 3e5), at(1e5 + 1, -2e5, 3e5),
                                              at(1e5, -2e5 + 1, 3e5), at(1e5, -2e5, 3e5 + 1)}}}};
    for (const Case& c : cases) {
        const Tet4Kinematics k = kinematicsOf(c.nodes);
        double scale = 0.0;
        std::array<double, 3> sum{};
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            const std::array<structural::InverseLength, 3> g = k.gradient(node);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                sum[axis] += g[axis].si();
                scale = std::max(scale, std::abs(g[axis].si()));
            }
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            INFO(c.label << " axis " << axis << " scale " << scale);
            // Scale-aware: the gradients of a thin element are large, so an
            // absolute bound would be meaningless across these fixtures.
            CHECK_THAT(sum[axis], WithinAbs(0.0, 1e-12 * std::max(scale, 1.0)));
        }
    }
}

TEST_CASE("Tet4Element_VolumeAgreesWithTheMeshingSignedVolume", "[structural][elem]") {
    // P16 owns the signed volume, and TET4_DERIVATION.md records that
    // `det J / 6` is the same triple product `meshing::signedVolume` forms. So
    // the two should agree to the last bit, and that is asserted rather than
    // approximated -- a tolerance here would hide a genuinely different
    // formula.
    for (const Nodes& nodes : {unitTet(), skewTet(), thinTet()}) {
        const Tet4Kinematics k = kinematicsOf(nodes);
        const Volume p16 =
            meshing::signedVolume(nodes[0], nodes[1], nodes[2], nodes[3]);
        INFO("p17 " << k.volume().si() << " p16 " << p16.si());
        CHECK(k.volume().si() == p16.si());
    }
}

// ---------------------------------------------------------------------------
// Geometry validity
// ---------------------------------------------------------------------------

TEST_CASE("Tet4Element_RejectsAnInvertedTetrahedronRatherThanReorderingIt",
          "[structural][elem]") {
    // The milestone's orientation requirement. P16's Netgen adapter already
    // applies one swap, so a mesh arriving here is positively oriented -- and
    // this kernel checks anyway, because the guarantee covers meshes that came
    // through P16 and not synthetic fixtures, future callers or an upstream
    // regression.
    Nodes nodes = unitTet();
    std::swap(nodes[1], nodes[2]);

    const Volume signed16 = meshing::signedVolume(nodes[0], nodes[1], nodes[2], nodes[3]);
    REQUIRE(signed16.si() < 0.0);

    CHECK(structural::tet4GeometryProblem(nodes) ==
          std::optional<ElementProblem>{ElementProblem::InvertedElement});
    const Result<Tet4Kinematics> k = structural::computeTet4Kinematics(nodes);
    REQUIRE_FALSE(k.has_value());
    CHECK(k.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(k.error().message, ContainsSubstring("inverted"));
    // And the whole chain refuses, not just the kinematics.
    CHECK_FALSE(structural::computeTet4Stiffness(nodes, constants(1.0, 0.0)).has_value());

    // NOT silently repaired: swapping the pair back is the caller's business
    // and gives a valid element, which proves the refusal was about the order
    // rather than about the points.
    std::swap(nodes[1], nodes[2]);
    CHECK(structural::computeTet4Kinematics(nodes).has_value());
}

TEST_CASE("Tet4Element_RejectsDegenerateAndNonFiniteGeometry", "[structural][elem]") {
    SECTION("four coplanar nodes") {
        const Nodes flat{at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(1, 1, 0)};
        REQUIRE(meshing::signedVolume(flat[0], flat[1], flat[2], flat[3]).si() == 0.0);
        CHECK(structural::tet4GeometryProblem(flat) ==
              std::optional<ElementProblem>{ElementProblem::DegenerateElement});
        const Result<Tet4Kinematics> k = structural::computeTet4Kinematics(flat);
        REQUIRE_FALSE(k.has_value());
        CHECK_THAT(k.error().message, ContainsSubstring("degenerate"));
    }

    SECTION("two coincident nodes") {
        const Nodes repeated{at(0, 0, 0), at(1, 0, 0), at(1, 0, 0), at(0, 0, 1)};
        CHECK(structural::tet4GeometryProblem(repeated) ==
              std::optional<ElementProblem>{ElementProblem::DegenerateElement});
    }

    SECTION("three collinear nodes and a fourth off the line") {
        const Nodes collinear{at(0, 0, 0), at(1, 0, 0), at(2, 0, 0), at(0, 1, 1)};
        CHECK(structural::tet4GeometryProblem(collinear) ==
              std::optional<ElementProblem>{ElementProblem::DegenerateElement});
    }

    SECTION("a non-finite coordinate is refused before any arithmetic") {
        for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()}) {
            Nodes nodes = unitTet();
            nodes[2] = at(0.0, bad, 0.0);
            INFO("coordinate " << bad);
            CHECK(structural::tet4GeometryProblem(nodes) ==
                  std::optional<ElementProblem>{ElementProblem::NonFiniteCoordinate});
            const Result<Tet4Kinematics> k = structural::computeTet4Kinematics(nodes);
            REQUIRE_FALSE(k.has_value());
            CHECK(k.error().code == ErrorCode::InvalidArgument);
        }
    }

    SECTION("a thin but positively oriented tetrahedron is ACCEPTED") {
        // `invalid` is not `poor quality`. P16 calls this data-valid -- its
        // degeneracy rule has no tolerance -- so refusing it here would make a
        // qualified mesh unsolvable, and P17-VALID-001 owns acceptance policy.
        const Nodes nodes = thinTet();
        REQUIRE(meshing::signedVolume(nodes[0], nodes[1], nodes[2], nodes[3]).si() > 0.0);
        CHECK_FALSE(structural::tet4GeometryProblem(nodes).has_value());
        const Tet4Kinematics k = kinematicsOf(nodes);
        CHECK(k.volume().si() > 0.0);
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            for (const structural::InverseLength g : k.gradient(node)) {
                CHECK(std::isfinite(g.si()));
            }
        }
    }

    SECTION("every ElementProblem has a name") {
        CHECK(structural::toString(ElementProblem::NonFiniteCoordinate) ==
              "non_finite_coordinate");
        CHECK(structural::toString(ElementProblem::DegenerateElement) == "degenerate_element");
        CHECK(structural::toString(ElementProblem::InvertedElement) == "inverted_element");
        CHECK(structural::toString(ElementProblem::NonFiniteResult) == "non_finite_result");
        CHECK(structural::toString(ElementProblem::InvalidMaterial) == "invalid_material");
    }
}

TEST_CASE("Tet4Element_ReflectedGeometryIsRejectedBecauseOrientationFlips",
          "[structural][elem]") {
    // An adversarial case the brief names: a reflection has det(R) = -1, so
    // with the node order kept fixed the signed volume flips and the element
    // must be refused. Restoring the order restores validity, which is what
    // makes this a statement about orientation and not about reflection.
    Nodes nodes = skewTet();
    for (Point3D& node : nodes) {
        node = at(-node.x.si(), node.y.si(), node.z.si());
    }
    CHECK(structural::tet4GeometryProblem(nodes) ==
          std::optional<ElementProblem>{ElementProblem::InvertedElement});
    std::swap(nodes[1], nodes[2]);
    CHECK_FALSE(structural::tet4GeometryProblem(nodes).has_value());
}

// ---------------------------------------------------------------------------
// The constitutive matrix
// ---------------------------------------------------------------------------

TEST_CASE("Tet4Element_ElasticityMatrixIsSymmetricAndMatchesTheLameFormulas",
          "[structural][elem]") {
    SECTION("E = 1, nu = 0 gives lambda = 0 and mu = 0.5 exactly") {
        // TET4_DERIVATION.md's simple case, exact in binary: lambda is
        // 1*0/((1)(1)) = 0 and mu is 1/2.
        const ElasticityMatrix d = elasticityOf(1.0, 0.0);
        CHECK(d.lame().si() == 0.0);
        CHECK(d.shear().si() == 0.5);
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                CHECK(d.d(i, j).si() == (i == j ? 1.0 : 0.0));
            }
        }
        for (std::size_t i = 3; i < kVoigtComponents; ++i) {
            CHECK(d.d(i, i).si() == 0.5);
        }
    }

    SECTION("symmetric, and zero off the blocks, at several ratios") {
        for (const double nu : {-0.2, 0.0, 0.25, 0.3, 0.45, 0.499}) {
            const ElasticityMatrix d = elasticityOf(210.0e9, nu);
            INFO("nu " << nu);
            for (std::size_t i = 0; i < kVoigtComponents; ++i) {
                for (std::size_t j = 0; j < kVoigtComponents; ++j) {
                    CHECK(d.d(i, j).si() == d.d(j, i).si());
                    if ((i < 3) != (j < 3)) {
                        CHECK(d.d(i, j).si() == 0.0);
                    }
                    if (i >= 3 && j >= 3 && i != j) {
                        CHECK(d.d(i, j).si() == 0.0);
                    }
                }
            }
        }
    }

    SECTION("lambda satisfies P15's own identity lambda = K - 2 mu / 3") {
        // An independent cross-check through P15's OTHER derived constant.
        // Production computes lambda from the frozen E/nu formula and takes mu
        // from P15's shearModulus; this ties both to the bulk modulus, which
        // neither used.
        for (const double nu : {-0.2, 0.0, 0.25, 0.3, 0.45}) {
            const materials::LinearElasticConstants c = constants(210.0e9, nu);
            const ElasticityMatrix d = elasticityOf(210.0e9, nu);
            const double identity = c.bulkModulus.si() - 2.0 * c.shearModulus.si() / 3.0;
            INFO("nu " << nu << " lambda " << d.lame().si() << " K - 2mu/3 " << identity);
            CHECK_THAT(d.lame().si(), WithinRel(identity, 1e-12));
        }
    }

    SECTION("mu is P15's shear modulus, not a recomputation") {
        for (const double nu : {0.0, 0.3, 0.45}) {
            const materials::LinearElasticConstants c = constants(210.0e9, nu);
            CHECK(elasticityOf(210.0e9, nu).shear().si() == c.shearModulus.si());
        }
    }

    SECTION("out-of-range ratios that make a Lame parameter non-finite are refused") {
        // The ranges themselves are P15's, which refuses them at entry
        // (P17-MAT-001). What a hand-filled struct can still do is make lambda
        // or mu infinite, and that must not reach Ke.
        for (const double nu : {0.5, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
            materials::LinearElasticConstants c;
            c.youngsModulus = ElasticModulus::fromSi(210.0e9);
            c.poissonRatio = PoissonRatio::of(nu);
            c.shearModulus = ElasticModulus::fromSi(210.0e9 / (2.0 * (1.0 + nu)));
            INFO("nu " << nu);
            const Result<ElasticityMatrix> d = structural::isotropicElasticity(c);
            REQUIRE_FALSE(d.has_value());
            CHECK(d.error().code == ErrorCode::InvalidArgument);
            CHECK_THAT(d.error().message, ContainsSubstring("finite"));
        }
    }
}

TEST_CASE("Tet4Element_EngineeringShearGivesTauEqualsMuGamma", "[structural][elem]") {
    // THE MANDATORY CONVENTION TEST. With engineering shear the factor of two
    // lives in gamma, so tau = mu gamma. A `2 mu` on the shear diagonal would
    // double every shear stress and leave the normal terms correct -- the
    // hardest kind of defect to see, which is why this is asserted directly
    // and has its own mutation probe.
    const double youngs = 210.0e9;
    const double nu = 0.3;
    const double mu = youngs / (2.0 * (1.0 + nu));
    const ElasticityMatrix d = elasticityOf(youngs, nu);
    const double gamma = 0.004;

    SECTION("each pure engineering shear maps to its own stress component") {
        const Stress6 xy = d.stressFrom(Strain6{.gammaXy = gamma});
        CHECK_THAT(xy.xy.si(), WithinRel(mu * gamma, 1e-12));
        CHECK_THAT(xy.xy.si(), !WithinRel(2.0 * mu * gamma, 1e-6));
        CHECK(xy.xx.si() == 0.0);
        CHECK(xy.yz.si() == 0.0);
        CHECK(xy.zx.si() == 0.0);

        const Stress6 yz = d.stressFrom(Strain6{.gammaYz = gamma});
        CHECK_THAT(yz.yz.si(), WithinRel(mu * gamma, 1e-12));
        CHECK(yz.xy.si() == 0.0);
        CHECK(yz.zx.si() == 0.0);

        const Stress6 zx = d.stressFrom(Strain6{.gammaZx = gamma});
        CHECK_THAT(zx.zx.si(), WithinRel(mu * gamma, 1e-12));
        CHECK(zx.xy.si() == 0.0);
        CHECK(zx.yz.si() == 0.0);
    }

    SECTION("uniaxial strain tests lambda independently") {
        const double lame = youngs * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
        const double e = 0.001;
        const Stress6 s = d.stressFrom(Strain6{.xx = e});
        CHECK_THAT(s.xx.si(), WithinRel((lame + 2.0 * mu) * e, 1e-12));
        CHECK_THAT(s.yy.si(), WithinRel(lame * e, 1e-12));
        CHECK_THAT(s.zz.si(), WithinRel(lame * e, 1e-12));
        CHECK(s.xy.si() == 0.0);
    }

    SECTION("volumetric strain tests the lambda coupling") {
        const double lame = youngs * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
        const double e = 0.0005;
        const Stress6 s = d.stressFrom(Strain6{.xx = e, .yy = e, .zz = e});
        const double expected = (3.0 * lame + 2.0 * mu) * e;
        CHECK_THAT(s.xx.si(), WithinRel(expected, 1e-12));
        CHECK_THAT(s.yy.si(), WithinRel(expected, 1e-12));
        CHECK_THAT(s.zz.si(), WithinRel(expected, 1e-12));
    }
}

// ---------------------------------------------------------------------------
// B against analytical strain
// ---------------------------------------------------------------------------

TEST_CASE("Tet4Element_ReproducesEveryAffineStrainStateExactly", "[structural][elem]") {
    // A Tet4 represents an affine displacement field exactly, so B applied to
    // nodal samples of one must return the field's own constant strain. The
    // analytical values come from differentiating the field by hand:
    //     exx = a1, eyy = b2, ezz = c3
    //     gxy = a2 + b1, gyz = b3 + c2, gzx = c1 + a3
    // Six pure cases freeze all six rows, and two mixed cases catch a pair
    // that cancels.
    struct Case {
        const char* label;
        std::array<double, 12> c;
        Strain6 expected;
    };
    const std::array<Case, 8> cases{{
        {"exx", {0, 2e-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, Strain6{.xx = 2e-3}},
        {"eyy", {0, 0, 0, 0, 0, 0, 3e-3, 0, 0, 0, 0, 0}, Strain6{.yy = 3e-3}},
        {"ezz", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5e-3}, Strain6{.zz = 5e-3}},
        {"gxy", {0, 0, 7e-3, 0, 0, 0, 0, 0, 0, 0, 0, 0}, Strain6{.gammaXy = 7e-3}},
        {"gyz", {0, 0, 0, 0, 0, 0, 0, 9e-3, 0, 0, 0, 0}, Strain6{.gammaYz = 9e-3}},
        {"gzx", {0, 0, 0, 0, 0, 0, 0, 0, 0, 11e-3, 0, 0}, Strain6{.gammaZx = 11e-3}},
        {"gxy from both halves",
         {0, 0, 4e-3, 0, 0, 6e-3, 0, 0, 0, 0, 0, 0},
         Strain6{.gammaXy = 10e-3}},
        {"mixed",
         {1e-4, 2e-3, 7e-3, 13e-3, -5e-4, 6e-3, 3e-3, 9e-3, 2e-4, 11e-3, 17e-3, 5e-3},
         Strain6{.xx = 2e-3,
                 .yy = 3e-3,
                 .zz = 5e-3,
                 .gammaXy = 7e-3 + 6e-3,
                 .gammaYz = 9e-3 + 17e-3,
                 .gammaZx = 11e-3 + 13e-3}},
    }};

    for (const Nodes& nodes : {unitTet(), skewTet()}) {
        const Tet4Kinematics k = kinematicsOf(nodes);
        for (const Case& c : cases) {
            const Strain6 actual = k.strainFrom(affine(nodes, c.c));
            INFO(c.label);
            CHECK_THAT(actual.xx, WithinAbs(c.expected.xx, 1e-14));
            CHECK_THAT(actual.yy, WithinAbs(c.expected.yy, 1e-14));
            CHECK_THAT(actual.zz, WithinAbs(c.expected.zz, 1e-14));
            CHECK_THAT(actual.gammaXy, WithinAbs(c.expected.gammaXy, 1e-14));
            CHECK_THAT(actual.gammaYz, WithinAbs(c.expected.gammaYz, 1e-14));
            CHECK_THAT(actual.gammaZx, WithinAbs(c.expected.gammaZx, 1e-14));
        }
    }
}

TEST_CASE("Tet4Element_BGivesZeroStrainForEveryRigidBodyMode", "[structural][elem]") {
    // The six modes checked through B, before Ke is considered at all: three
    // translations and three infinitesimal rotations.
    for (const Nodes& nodes : {unitTet(), skewTet()}) {
        const Tet4Kinematics k = kinematicsOf(nodes);
        const std::array<Eigen::Matrix<double, 12, 1>, 6> modes = rigidModes(nodes);
        const char* names[] = {"Tx", "Ty", "Tz", "Rx", "Ry", "Rz"};
        for (std::size_t m = 0; m < modes.size(); ++m) {
            const Strain6 strain = k.strainFrom(toDisplacements(modes[m]));
            const double scale = productionB(k).cwiseAbs().maxCoeff() * modes[m].norm();
            const double tolerance = 1e-12 * std::max(scale, 1.0);
            INFO(names[m] << " scale " << scale);
            CHECK_THAT(strain.xx, WithinAbs(0.0, tolerance));
            CHECK_THAT(strain.yy, WithinAbs(0.0, tolerance));
            CHECK_THAT(strain.zz, WithinAbs(0.0, tolerance));
            CHECK_THAT(strain.gammaXy, WithinAbs(0.0, tolerance));
            CHECK_THAT(strain.gammaYz, WithinAbs(0.0, tolerance));
            CHECK_THAT(strain.gammaZx, WithinAbs(0.0, tolerance));
        }
    }
}

TEST_CASE("Tet4Element_BRowOrderMatchesTheSharedVoigtOrdering", "[structural][elem]") {
    // The row index of B is a `TensorComponent`, and P17-DATA-001 froze that
    // enum's integer values. Asserted here so a reordering there breaks this
    // test rather than silently permuting the stress components.
    STATIC_REQUIRE(static_cast<std::size_t>(structural::TensorComponent::XX) == 0);
    STATIC_REQUIRE(static_cast<std::size_t>(structural::TensorComponent::YY) == 1);
    STATIC_REQUIRE(static_cast<std::size_t>(structural::TensorComponent::ZZ) == 2);
    STATIC_REQUIRE(static_cast<std::size_t>(structural::TensorComponent::XY) == 3);
    STATIC_REQUIRE(static_cast<std::size_t>(structural::TensorComponent::YZ) == 4);
    STATIC_REQUIRE(static_cast<std::size_t>(structural::TensorComponent::ZX) == 5);

    // And the normal rows read the matching gradient component while the shear
    // rows pair the two the engineering definition pairs.
    const Tet4Kinematics k = kinematicsOf(skewTet());
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        const std::array<structural::InverseLength, 3> g = k.gradient(node);
        const std::size_t ux = localDofIndex(node, DofComponent::Ux);
        const std::size_t uy = localDofIndex(node, DofComponent::Uy);
        const std::size_t uz = localDofIndex(node, DofComponent::Uz);
        INFO("node " << node);
        CHECK(k.b(0, ux).si() == g[0].si());
        CHECK(k.b(1, uy).si() == g[1].si());
        CHECK(k.b(2, uz).si() == g[2].si());
        CHECK(k.b(3, ux).si() == g[1].si());
        CHECK(k.b(3, uy).si() == g[0].si());
        CHECK(k.b(4, uy).si() == g[2].si());
        CHECK(k.b(4, uz).si() == g[1].si());
        CHECK(k.b(5, uz).si() == g[0].si());
        CHECK(k.b(5, ux).si() == g[2].si());
        // And the entries that must be zero.
        CHECK(k.b(0, uy).si() == 0.0);
        CHECK(k.b(1, uz).si() == 0.0);
        CHECK(k.b(2, ux).si() == 0.0);
        CHECK(k.b(3, uz).si() == 0.0);
        CHECK(k.b(4, ux).si() == 0.0);
        CHECK(k.b(5, uy).si() == 0.0);
    }
    // Out of range is zero.
    CHECK(k.b(kVoigtComponents, 0).si() == 0.0);
    CHECK(k.b(0, kTet4Dofs).si() == 0.0);
}

TEST_CASE("Tet4Element_LocalDofOrderingMatchesTheGlobalNumbering", "[structural][elem]") {
    // The local order must be the global one or an assembler scatters an
    // element's twelve entries into the wrong rows (ADR-037).
    STATIC_REQUIRE(kTet4Dofs == 12);
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        CHECK(localDofIndex(node, DofComponent::Ux) == 3 * node + 0);
        CHECK(localDofIndex(node, DofComponent::Uy) == 3 * node + 1);
        CHECK(localDofIndex(node, DofComponent::Uz) == 3 * node + 2);
    }
    CHECK(localDofIndex(3, DofComponent::Uz) == 11);

    // The relation to P17-DOF's GLOBAL numbering, which is `3k + c + 1` for a
    // node at ordinal k: a local index is the global index of the same
    // component of node k, minus one, when the element's nodes are the first
    // four of the mesh. Checked through the global map rather than restated.
    meshing::MeshBuilder builder;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(builder.addNode(at(static_cast<double>(i) + 1.0, 0.0, 0.0)).has_value());
    }
    const meshing::Mesh mesh = builder.build();
    const Result<structural::MeshDofMap> map = structural::buildMeshDofMap(mesh);
    REQUIRE(map.has_value());
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        for (const DofComponent component : structural::kDofComponents) {
            const Result<structural::DofIndex> global = map->indexOf(
                structural::NodalDof{.node = map->nodeAt(node), .component = component});
            REQUIRE(global.has_value());
            INFO("node " << node << " component " << structural::toString(component));
            CHECK(localDofIndex(node, component) + 1 == global->value());
        }
    }

    // And a displacement in exactly one local slot reaches exactly the
    // expected column of B.
    const Tet4Kinematics k = kinematicsOf(skewTet());
    Displacements only{};
    only[2] = move(0.0, 1.0, 0.0); // node 2, Uy -> local index 7
    const Strain6 strain = k.strainFrom(only);
    CHECK_THAT(strain.yy, WithinRel(k.b(1, 7).si(), 1e-15));
    CHECK_THAT(strain.gammaXy, WithinRel(k.b(3, 7).si(), 1e-15));
}

// ---------------------------------------------------------------------------
// Ke
// ---------------------------------------------------------------------------

TEST_CASE("Tet4Element_StiffnessIsSymmetricAndFinite", "[structural][elem]") {
    // Symmetric because `B^T D B` is, with D symmetric -- NOT because it is
    // symmetrised. The residual is recorded relative to the matrix norm,
    // because an absolute bound is meaningless across a 210 GPa steel element
    // and a unit-material one.
    struct Case {
        const char* label;
        Nodes nodes;
        double youngs;
        double nu;
    };
    const std::array<Case, 5> cases{{{"unit, E=1 nu=0", unitTet(), 1.0, 0.0},
                                     {"unit, steel", unitTet(), 210.0e9, 0.3},
                                     {"skew, steel", skewTet(), 210.0e9, 0.3},
                                     {"thin, steel", thinTet(), 210.0e9, 0.3},
                                     {"skew, near-incompressible", skewTet(), 210.0e9, 0.499}}};
    for (const Case& c : cases) {
        const Tet4Stiffness ke = stiffnessOf(c.nodes, c.youngs, c.nu);
        const Eigen::Matrix<double, 12, 12> m = productionKe(ke);
        const double norm = m.cwiseAbs().maxCoeff();
        const double residual = (m - m.transpose()).cwiseAbs().maxCoeff();
        INFO(c.label << ": ||Ke||inf " << norm << ", max|Ke_ij - Ke_ji| " << residual
                     << ", relative " << residual / norm);
        CHECK(m.allFinite());
        CHECK(norm > 0.0);
        CHECK(residual <= 1e-12 * norm);
        // Out of range reads zero rather than past the end.
        CHECK(ke(kTet4Dofs, 0).si() == 0.0);
        CHECK(ke(0, kTet4Dofs).si() == 0.0);
    }
}

TEST_CASE("Tet4Element_StiffnessMatchesTheAnalyticalUnitTetrahedron", "[structural][elem]") {
    // The one fixture whose Ke can be written down. For the reference element
    // with E = 1, nu = 0: V = 1/6, lambda = 0, mu = 1/2, so
    //     D = diag(1,1,1,1/2,1/2,1/2)
    // and B's entries are 0, +1 and -1. Every Ke entry is therefore a sixth of
    // a small rational, and the expected values below are formed from the
    // analytical gradients by hand -- not read from production.
    const Tet4Kinematics k = kinematicsOf(unitTet());
    const Tet4Stiffness ke = stiffnessOf(unitTet(), 1.0, 0.0);

    CHECK(k.volume().si() == 1.0 / 6.0);

    // Build the expected matrix from the ANALYTICAL gradients and the
    // ANALYTICAL D, with the B block written out again here.
    const std::array<std::array<double, 3>, 4> grad{
        {{-1.0, -1.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    Eigen::Matrix<double, 6, 12> b = Eigen::Matrix<double, 6, 12>::Zero();
    for (int n = 0; n < 4; ++n) {
        const double bi = grad[static_cast<std::size_t>(n)][0];
        const double ci = grad[static_cast<std::size_t>(n)][1];
        const double di = grad[static_cast<std::size_t>(n)][2];
        b(0, 3 * n + 0) = bi;
        b(1, 3 * n + 1) = ci;
        b(2, 3 * n + 2) = di;
        b(3, 3 * n + 0) = ci;
        b(3, 3 * n + 1) = bi;
        b(4, 3 * n + 1) = di;
        b(4, 3 * n + 2) = ci;
        b(5, 3 * n + 2) = bi;
        b(5, 3 * n + 0) = di;
    }
    Eigen::Matrix<double, 6, 6> d = Eigen::Matrix<double, 6, 6>::Zero();
    d(0, 0) = d(1, 1) = d(2, 2) = 1.0;
    d(3, 3) = d(4, 4) = d(5, 5) = 0.5;
    const Eigen::Matrix<double, 12, 12> expected =
        (1.0 / 6.0) * b.transpose() * d * b;

    const Eigen::Matrix<double, 12, 12> actual = productionKe(ke);
    const double error = (actual - expected).cwiseAbs().maxCoeff();
    INFO("max |Ke - Ke_analytical| = " << error);
    CHECK(error <= 1e-15);

    // Two entries written out as rationals, so a reader can check the chain by
    // hand. Node 1 (ordinal 1) has grad = (1,0,0), so its Ux row sees
    // D(0,0) * 1 * 1 = 1 in the exx row and nothing else, giving V * 1 = 1/6.
    CHECK_THAT(ke(3, 3).si(), WithinAbs(1.0 / 6.0, 1e-16));
    // Node 1 Ux against node 2 Uy: only the gxy row couples them, with
    // c1 * b2 = 0 * 1, so the entry is zero.
    CHECK_THAT(ke(3, 7).si(), WithinAbs(0.0, 1e-16));
}

TEST_CASE("Tet4Element_MatchesTheIndependentReferenceImplementation", "[structural][elem]") {
    // THE INDEPENDENT ROUTE: a 4x4 coordinate-matrix inverse instead of a 3x3
    // Jacobian, lambda and mu computed from E and nu instead of taken from
    // P15, and Eigen products instead of hand loops. See this file's header.
    struct Case {
        const char* label;
        Nodes nodes;
        double youngs;
        double nu;
    };
    std::vector<Case> cases{
        {"unit, E=1 nu=0", unitTet(), 1.0, 0.0},
        {"unit, steel", unitTet(), 210.0e9, 0.3},
        {"skew, steel", skewTet(), 210.0e9, 0.3},
        {"skew, auxetic", skewTet(), 70.0e9, -0.2},
        {"skew, nu=0.45", skewTet(), 70.0e9, 0.45},
        {"skew, nu=0.499", skewTet(), 70.0e9, 0.499},
        {"skew, nu=0.25", skewTet(), 1.0e6, 0.25},
        {"thin, steel", thinTet(), 210.0e9, 0.3},
        {"translated skew", skewTet(), 210.0e9, 0.3},
        {"scaled skew", skewTet(), 210.0e9, 0.3},
    };
    for (Point3D& node : cases[8].nodes) {
        node = at(node.x.si() + 13.7, node.y.si() - 4.2, node.z.si() + 8.9);
    }
    for (Point3D& node : cases[9].nodes) {
        node = at(node.x.si() * 1e-3, node.y.si() * 1e-3, node.z.si() * 1e-3);
    }

    // Deterministic pseudo-random well-conditioned tetrahedra, as breadth on
    // top of the fixed fixtures rather than instead of them. Fixed seed, and
    // positive volume enforced by construction.
    std::mt19937 engine{20261007U};
    std::uniform_real_distribution<double> spread{-1.0, 1.0};
    for (int i = 0; i < 12; ++i) {
        Nodes nodes{};
        for (;;) {
            for (Point3D& node : nodes) {
                node = at(spread(engine), spread(engine), spread(engine));
            }
            const double v = meshing::signedVolume(nodes[0], nodes[1], nodes[2], nodes[3]).si();
            if (v > 0.02) {
                break;
            }
            if (v < -0.02) {
                std::swap(nodes[1], nodes[2]);
                break;
            }
        }
        cases.push_back({"random", nodes, 210.0e9, 0.3});
    }

    for (const Case& c : cases) {
        const Reference expected = reference(c.nodes, c.youngs, c.nu);
        const Tet4Kinematics k = kinematicsOf(c.nodes);
        const ElasticityMatrix e = elasticityOf(c.youngs, c.nu);
        const Tet4Stiffness ke = stiffnessOf(c.nodes, c.youngs, c.nu);

        const double volumeError =
            std::abs(k.volume().si() - expected.volume) / std::abs(expected.volume);

        Eigen::Matrix<double, 4, 3> gradients;
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            const std::array<structural::InverseLength, 3> g = k.gradient(node);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                gradients(static_cast<int>(node), static_cast<int>(axis)) = g[axis].si();
            }
        }
        const double gradientError = (gradients - expected.gradients).cwiseAbs().maxCoeff() /
                                     std::max(expected.gradients.cwiseAbs().maxCoeff(), 1.0);
        const double bError = (productionB(k) - expected.b).cwiseAbs().maxCoeff() /
                              std::max(expected.b.cwiseAbs().maxCoeff(), 1.0);
        const double dError = (productionD(e) - expected.d).cwiseAbs().maxCoeff() /
                              std::max(expected.d.cwiseAbs().maxCoeff(), 1.0);
        const double keError = (productionKe(ke) - expected.ke).cwiseAbs().maxCoeff() /
                               std::max(expected.ke.cwiseAbs().maxCoeff(), 1.0);

        INFO(c.label << ": V " << volumeError << ", grad " << gradientError << ", B " << bError
                     << ", D " << dError << ", Ke " << keError);
        CHECK(volumeError <= 1e-13);
        CHECK(gradientError <= 1e-11);
        CHECK(bError <= 1e-11);
        CHECK(dError <= 1e-14);
        CHECK(keError <= 1e-11);
    }
}

TEST_CASE("Tet4Element_HasSixRigidBodyModesAndNoMore", "[structural][elem]") {
    // Ke r = 0 for each analytically constructed mode, and then the eigenvalue
    // structure: exactly six zeros and six positives. The modes are tested
    // explicitly rather than inferred from the eigenvalues, because a
    // nullspace of the right DIMENSION could still be the wrong nullspace.
    for (const Nodes& nodes : {unitTet(), skewTet()}) {
        const Tet4Stiffness ke = stiffnessOf(nodes, 210.0e9, 0.3);
        const Eigen::Matrix<double, 12, 12> m = productionKe(ke);
        const double norm = m.cwiseAbs().maxCoeff();
        const std::array<Eigen::Matrix<double, 12, 1>, 6> modes = rigidModes(nodes);
        const char* names[] = {"Tx", "Ty", "Tz", "Rx", "Ry", "Rz"};

        for (std::size_t i = 0; i < modes.size(); ++i) {
            const double residual = (m * modes[i]).cwiseAbs().maxCoeff();
            const Energy energy = ke.energyFrom(toDisplacements(modes[i]));
            INFO(names[i] << ": ||Ke r||inf " << residual << " relative "
                          << residual / (norm * modes[i].norm()) << ", U " << energy.si());
            CHECK(residual <= 1e-12 * norm * modes[i].norm());
            CHECK(std::abs(energy.si()) <= 1e-12 * norm * modes[i].squaredNorm());
        }

        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 12, 12>> solver(m);
        REQUIRE(solver.info() == Eigen::Success);
        const Eigen::Matrix<double, 12, 1> values = solver.eigenvalues();
        const double largest = values.cwiseAbs().maxCoeff();
        // Scale-aware: roundoff-level negatives are not physical instability,
        // but a materially negative eigenvalue is a failure.
        const double zeroBand = 1e-10 * largest;
        int zeros = 0;
        int positives = 0;
        for (int i = 0; i < 12; ++i) {
            if (std::abs(values(i)) <= zeroBand) {
                ++zeros;
            } else if (values(i) > 0.0) {
                ++positives;
            } else {
                FAIL("eigenvalue " << i << " is materially negative: " << values(i)
                                   << " against largest " << largest);
            }
        }
        INFO("eigenvalues " << values.transpose() << ", zero band " << zeroBand);
        CHECK(zeros == 6);
        CHECK(positives == 6);

        // Numerical rank, by SVD: an independent decomposition of the same
        // claim.
        Eigen::JacobiSVD<Eigen::Matrix<double, 12, 12>> svd(m);
        const Eigen::Matrix<double, 12, 1> singular = svd.singularValues();
        int rank = 0;
        for (int i = 0; i < 12; ++i) {
            if (singular(i) > 1e-10 * singular(0)) {
                ++rank;
            }
        }
        INFO("singular values " << singular.transpose());
        CHECK(rank == 6);
    }
}

TEST_CASE("Tet4Element_IsPositiveSemidefiniteAndStoresRealEnergy", "[structural][elem]") {
    const Nodes nodes = skewTet();
    const Tet4Kinematics k = kinematicsOf(nodes);
    const ElasticityMatrix e = elasticityOf(210.0e9, 0.3);
    const Tet4Stiffness ke = stiffnessOf(nodes, 210.0e9, 0.3);
    const Eigen::Matrix<double, 12, 12> m = productionKe(ke);
    const double norm = m.cwiseAbs().maxCoeff();

    SECTION("u^T Ke u is never materially negative, over deterministic samples") {
        std::mt19937 engine{7U};
        std::uniform_real_distribution<double> spread{-1.0e-3, 1.0e-3};
        for (int trial = 0; trial < 64; ++trial) {
            Eigen::Matrix<double, 12, 1> u;
            for (int i = 0; i < 12; ++i) {
                u(i) = spread(engine);
            }
            const double quadratic = u.transpose() * m * u;
            INFO("trial " << trial << " u^T Ke u = " << quadratic);
            CHECK(quadratic >= -1e-12 * norm * u.squaredNorm());
        }
    }

    SECTION("a deformation stores positive energy") {
        // Pure extension along x: not a rigid motion, so U > 0 strictly.
        const Displacements u = affine(nodes, {0, 1e-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        const Energy energy = ke.energyFrom(u);
        INFO("U = " << energy.si() << " J");
        CHECK(energy.si() > 0.0);
    }

    SECTION("energy from Ke equals V/2 eps^T D eps") {
        // The end-to-end formulation check: the quadratic form in nodal
        // displacements and the strain-energy density integral must agree.
        const std::array<std::array<double, 12>, 3> fields{
            {{0, 2e-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {0, 0, 7e-3, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {1e-4, 2e-3, 7e-3, 13e-3, -5e-4, 6e-3, 3e-3, 9e-3, 2e-4, 11e-3, 17e-3, 5e-3}}};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Displacements u = affine(nodes, fields[i]);
            const Strain6 strain = k.strainFrom(u);
            const Stress6 stress = e.stressFrom(strain);
            const double density = strain.xx * stress.xx.si() + strain.yy * stress.yy.si() +
                                   strain.zz * stress.zz.si() +
                                   strain.gammaXy * stress.xy.si() +
                                   strain.gammaYz * stress.yz.si() +
                                   strain.gammaZx * stress.zx.si();
            const double fromStrain = 0.5 * k.volume().si() * density;
            const double fromStiffness = ke.energyFrom(u).si();
            INFO("field " << i << ": from Ke " << fromStiffness << ", from strain "
                          << fromStrain);
            CHECK_THAT(fromStiffness, WithinRel(fromStrain, 1e-10));
            CHECK(fromStiffness > 0.0);
        }
    }

    SECTION("virtual work is reciprocal") {
        const Displacements u = affine(nodes, {0, 2e-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        const Displacements v = affine(nodes, {0, 0, 5e-3, 0, 0, 0, 1e-3, 0, 0, 0, 0, 3e-3});
        const std::array<Force3D, kTet4Nodes> fu = ke.forceFrom(u);
        const std::array<Force3D, kTet4Nodes> fv = ke.forceFrom(v);
        double vKu = 0.0;
        double uKv = 0.0;
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            vKu += v[node].x.si() * fu[node].x.si() + v[node].y.si() * fu[node].y.si() +
                   v[node].z.si() * fu[node].z.si();
            uKv += u[node].x.si() * fv[node].x.si() + u[node].y.si() * fv[node].y.si() +
                   u[node].z.si() * fv[node].z.si();
        }
        INFO("v^T Ke u = " << vKu << ", u^T Ke v = " << uKv);
        CHECK_THAT(vKu, WithinRel(uKv, 1e-12));
    }

    SECTION("a rigid translation implies no nodal force") {
        const std::array<Force3D, kTet4Nodes> f =
            ke.forceFrom({move(1e-3, -2e-3, 5e-4), move(1e-3, -2e-3, 5e-4),
                          move(1e-3, -2e-3, 5e-4), move(1e-3, -2e-3, 5e-4)});
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            INFO("node " << node);
            CHECK_THAT(f[node].x.si(), WithinAbs(0.0, 1e-12 * norm * 1e-3));
            CHECK_THAT(f[node].y.si(), WithinAbs(0.0, 1e-12 * norm * 1e-3));
            CHECK_THAT(f[node].z.si(), WithinAbs(0.0, 1e-12 * norm * 1e-3));
        }
    }
}

// ---------------------------------------------------------------------------
// Invariance and scaling
// ---------------------------------------------------------------------------

TEST_CASE("Tet4Element_IsInvariantUnderTranslation", "[structural][elem]") {
    // Gradients are derivatives, so they cannot depend on where the origin is.
    // A nontrivial offset, and a far-from-origin case where subtractive
    // cancellation is the real risk.
    const Nodes base = skewTet();
    const Tet4Kinematics k0 = kinematicsOf(base);
    const Tet4Stiffness ke0 = stiffnessOf(base, 210.0e9, 0.3);
    const Eigen::Matrix<double, 12, 12> m0 = productionKe(ke0);

    struct Shift {
        const char* label;
        double x, y, z;
        double tolerance;
    };
    const std::array<Shift, 3> shifts{{{"modest", 13.7, -4.2, 8.9, 1e-14},
                                       {"kilometres", 1.0e3, -2.0e3, 5.0e3, 1e-12},
                                       {"far field", 1.0e6, -1.0e6, 1.0e6, 1e-9}}};
    for (const Shift& s : shifts) {
        Nodes moved = base;
        for (Point3D& node : moved) {
            node = at(node.x.si() + s.x, node.y.si() + s.y, node.z.si() + s.z);
        }
        const Tet4Kinematics k = kinematicsOf(moved);
        const Eigen::Matrix<double, 12, 12> m = productionKe(stiffnessOf(moved, 210.0e9, 0.3));
        const double volumeError = std::abs(k.volume().si() - k0.volume().si()) / k0.volume().si();
        const double bError = (productionB(k) - productionB(k0)).cwiseAbs().maxCoeff() /
                              productionB(k0).cwiseAbs().maxCoeff();
        const double keError = (m - m0).cwiseAbs().maxCoeff() / m0.cwiseAbs().maxCoeff();
        INFO(s.label << ": V " << volumeError << ", B " << bError << ", Ke " << keError);
        CHECK(volumeError <= s.tolerance);
        CHECK(bError <= s.tolerance);
        CHECK(keError <= s.tolerance);
    }
}

TEST_CASE("Tet4Element_FollowsTheGeometryScaleLaw", "[structural][elem]") {
    // `x' = s x` gives `V' = s^3 V`, `B' = B/s` and therefore `Ke' = s Ke`,
    // because `s^3 * (1/s) * (1/s) = s`. This is the sharpest units check in
    // the milestone: dropping V from Ke, or taking |V|, breaks it.
    const Nodes base = skewTet();
    const Tet4Kinematics k0 = kinematicsOf(base);
    const Eigen::Matrix<double, 12, 12> m0 = productionKe(stiffnessOf(base, 210.0e9, 0.3));
    const double b0 = productionB(k0).cwiseAbs().maxCoeff();
    const double n0 = m0.cwiseAbs().maxCoeff();

    for (const double s : {1.0e-3, 1.0e-1, 1.0, 1.0e2, 1.0e3}) {
        Nodes scaled = base;
        for (Point3D& node : scaled) {
            node = at(node.x.si() * s, node.y.si() * s, node.z.si() * s);
        }
        const Tet4Kinematics k = kinematicsOf(scaled);
        const Eigen::Matrix<double, 12, 12> m = productionKe(stiffnessOf(scaled, 210.0e9, 0.3));
        const double volumeRatio = k.volume().si() / k0.volume().si();
        const double bRatio = productionB(k).cwiseAbs().maxCoeff() / b0;
        const double keRatio = m.cwiseAbs().maxCoeff() / n0;
        INFO("s " << s << ": V ratio " << volumeRatio << " (expected " << s * s * s
                  << "), B ratio " << bRatio << " (expected " << 1.0 / s << "), Ke ratio "
                  << keRatio << " (expected " << s << ")");
        CHECK_THAT(volumeRatio, WithinRel(s * s * s, 1e-12));
        CHECK_THAT(bRatio, WithinRel(1.0 / s, 1e-12));
        CHECK_THAT(keRatio, WithinRel(s, 1e-12));
    }
}

TEST_CASE("Tet4Element_FollowsTheMaterialScaleLaw", "[structural][elem]") {
    // `D` is linear in `E` at fixed `nu`, so `Ke` is too.
    const Nodes nodes = skewTet();
    const double base = 1.0e9;
    const ElasticityMatrix d0 = elasticityOf(base, 0.3);
    const Eigen::Matrix<double, 12, 12> m0 = productionKe(stiffnessOf(nodes, base, 0.3));

    for (const double factor : {1.0e-3, 1.0, 7.0, 1000.0}) {
        const ElasticityMatrix d = elasticityOf(base * factor, 0.3);
        const Eigen::Matrix<double, 12, 12> m =
            productionKe(stiffnessOf(nodes, base * factor, 0.3));
        const double dRatio = productionD(d).cwiseAbs().maxCoeff() /
                              productionD(d0).cwiseAbs().maxCoeff();
        const double keRatio = m.cwiseAbs().maxCoeff() / m0.cwiseAbs().maxCoeff();
        INFO("E factor " << factor << ": D ratio " << dRatio << ", Ke ratio " << keRatio);
        CHECK_THAT(dRatio, WithinRel(factor, 1e-12));
        CHECK_THAT(keRatio, WithinRel(factor, 1e-12));
    }
}

TEST_CASE("Tet4Element_IsCovariantUnderAProperRotation", "[structural][elem]") {
    // A rotation turns the global component basis with the geometry, so
    // `Ke_rot = T Ke T^T` with `T` the block-diagonal rotation of the twelve
    // degrees of freedom -- NOT `Ke_rot = Ke`. Requiring the latter would be
    // wrong, so the covariance is tested, plus the four invariants that hold
    // regardless.
    const Nodes base = skewTet();
    const double angle = 0.7;
    Eigen::Matrix3d r;
    r = Eigen::AngleAxisd(angle, Eigen::Vector3d(1.0, 2.0, -3.0).normalized());
    REQUIRE_THAT(r.determinant(), WithinRel(1.0, 1e-14));

    Nodes rotated{};
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        const Eigen::Vector3d x(base[i].x.si(), base[i].y.si(), base[i].z.si());
        const Eigen::Vector3d y = r * x;
        rotated[i] = at(y(0), y(1), y(2));
    }

    const Tet4Kinematics k0 = kinematicsOf(base);
    const Tet4Kinematics k1 = kinematicsOf(rotated);
    const Eigen::Matrix<double, 12, 12> m0 = productionKe(stiffnessOf(base, 210.0e9, 0.3));
    const Eigen::Matrix<double, 12, 12> m1 = productionKe(stiffnessOf(rotated, 210.0e9, 0.3));

    Eigen::Matrix<double, 12, 12> t = Eigen::Matrix<double, 12, 12>::Zero();
    for (int node = 0; node < 4; ++node) {
        t.block<3, 3>(3 * node, 3 * node) = r;
    }

    const double covariance = (m1 - t * m0 * t.transpose()).cwiseAbs().maxCoeff() /
                              m0.cwiseAbs().maxCoeff();
    INFO("||Ke_rot - T Ke T^T|| / ||Ke|| = " << covariance);
    CHECK(covariance <= 1e-12);

    // Volume invariant.
    CHECK_THAT(k1.volume().si(), WithinRel(k0.volume().si(), 1e-13));
    // Eigenvalues invariant, because T is orthogonal.
    const Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 12, 12>> e0(m0);
    const Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 12, 12>> e1(m1);
    const double spectral = (e1.eigenvalues() - e0.eigenvalues()).cwiseAbs().maxCoeff() /
                            e0.eigenvalues().cwiseAbs().maxCoeff();
    INFO("eigenvalue drift " << spectral);
    CHECK(spectral <= 1e-11);
    // Strain energy invariant under the correspondingly rotated displacement.
    const Displacements u = affine(base, {0, 2e-3, 7e-3, 0, 0, 1e-3, 3e-3, 0, 0, 0, 0, 5e-3});
    Displacements ur{};
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        const Eigen::Vector3d d(u[i].x.si(), u[i].y.si(), u[i].z.si());
        const Eigen::Vector3d rd = r * d;
        ur[i] = move(rd(0), rd(1), rd(2));
    }
    const Tet4Stiffness ke0 = stiffnessOf(base, 210.0e9, 0.3);
    const Tet4Stiffness ke1 = stiffnessOf(rotated, 210.0e9, 0.3);
    INFO("U " << ke0.energyFrom(u).si() << " vs " << ke1.energyFrom(ur).si());
    CHECK_THAT(ke1.energyFrom(ur).si(), WithinRel(ke0.energyFrom(u).si(), 1e-10));
}

TEST_CASE("Tet4Element_GivesTheSameStiffnessForEquivalentUnitInputs", "[structural][elem]") {
    // The same physical element described through different units must give
    // the same Ke, because the quantity types convert at construction.
    // 100 mm == 0.1 m, and 200 GPa == 2e11 Pa.
    const Nodes metres{at(0, 0, 0), at(0.1, 0, 0), at(0, 0.1, 0), at(0, 0, 0.1)};
    const Nodes millimetres{Point3D{0_mm, 0_mm, 0_mm}, Point3D{100_mm, 0_mm, 0_mm},
                            Point3D{0_mm, 100_mm, 0_mm}, Point3D{0_mm, 0_mm, 100_mm}};

    materials::LinearElasticConstants pascals = constants(2.0e11, 0.3);
    materials::LinearElasticConstants gigapascals = constants(2.0e11, 0.3);
    gigapascals.youngsModulus = 200_GPa;
    REQUIRE(pascals.youngsModulus.si() == gigapascals.youngsModulus.si());

    Result<Tet4Stiffness> inMetres = structural::computeTet4Stiffness(metres, pascals);
    Result<Tet4Stiffness> inMillimetres =
        structural::computeTet4Stiffness(millimetres, gigapascals);
    REQUIRE(inMetres.has_value());
    REQUIRE(inMillimetres.has_value());
    const Eigen::Matrix<double, 12, 12> a = productionKe(*inMetres);
    const Eigen::Matrix<double, 12, 12> b = productionKe(*inMillimetres);
    const double error = (a - b).cwiseAbs().maxCoeff() / a.cwiseAbs().maxCoeff();
    INFO("relative difference " << error);
    CHECK(error <= 1e-15);
}

// ---------------------------------------------------------------------------
// Permutations and determinism
// ---------------------------------------------------------------------------

TEST_CASE("Tet4Element_IsEquivalentUnderAnOrientationPreservingNodePermutation",
          "[structural][elem]") {
    // An EVEN permutation keeps the signed volume positive and relabels the
    // local degrees of freedom, so `Ke_permuted = P Ke P^T`. This catches an
    // implementation that assumed something about a particular vertex label --
    // node 0 is the Jacobian's origin, and nothing else may depend on it.
    const Nodes base = skewTet();
    // The 3-cycle (1 2 3) on the last three nodes: even, so orientation holds.
    const Nodes cycled{base[0], base[2], base[3], base[1]};
    REQUIRE(meshing::signedVolume(cycled[0], cycled[1], cycled[2], cycled[3]).si() > 0.0);

    const Eigen::Matrix<double, 12, 12> m0 = productionKe(stiffnessOf(base, 210.0e9, 0.3));
    const Eigen::Matrix<double, 12, 12> m1 = productionKe(stiffnessOf(cycled, 210.0e9, 0.3));

    // P maps the permuted numbering back to the original: permuted node i is
    // original node `source[i]`.
    const std::array<int, 4> source{0, 2, 3, 1};
    Eigen::Matrix<double, 12, 12> p = Eigen::Matrix<double, 12, 12>::Zero();
    for (int node = 0; node < 4; ++node) {
        for (int axis = 0; axis < 3; ++axis) {
            p(3 * node + axis, 3 * source[static_cast<std::size_t>(node)] + axis) = 1.0;
        }
    }
    const double error = (m1 - p * m0 * p.transpose()).cwiseAbs().maxCoeff() /
                         m0.cwiseAbs().maxCoeff();
    INFO("||Ke_perm - P Ke P^T|| / ||Ke|| = " << error);
    CHECK(error <= 1e-12);

    // Volume and eigenvalues are unchanged by a relabelling.
    CHECK_THAT(kinematicsOf(cycled).volume().si(),
               WithinRel(kinematicsOf(base).volume().si(), 1e-13));
}

TEST_CASE("Tet4Element_RejectsAnOddNodePermutation", "[structural][elem]") {
    // An ODD swap flips the signed volume, so the element is refused -- even
    // though one could mathematically reorder it back. That is the orientation
    // rule, and refusing is what keeps P16 the owner of mesh correction.
    const Nodes base = skewTet();
    const std::array<std::array<std::size_t, 4>, 3> odd{
        {{1, 0, 2, 3}, {0, 2, 1, 3}, {0, 1, 3, 2}}};
    for (const std::array<std::size_t, 4>& order : odd) {
        const Nodes swapped{base[order[0]], base[order[1]], base[order[2]], base[order[3]]};
        INFO("order " << order[0] << order[1] << order[2] << order[3]);
        CHECK(meshing::signedVolume(swapped[0], swapped[1], swapped[2], swapped[3]).si() < 0.0);
        CHECK(structural::tet4GeometryProblem(swapped) ==
              std::optional<ElementProblem>{ElementProblem::InvertedElement});
    }
}

TEST_CASE("Tet4Element_IsDeterministicOverRepeatedEvaluation", "[structural][elem]") {
    // The operations are a fixed sequence of scalar arithmetic with no state,
    // so repetition within one build should be bit-identical -- and that is
    // what is asserted, rather than a tolerance that would hide a dependence
    // on something that varies.
    for (const Nodes& nodes : {unitTet(), skewTet(), thinTet()}) {
        const Tet4Kinematics first = kinematicsOf(nodes);
        const ElasticityMatrix firstD = elasticityOf(210.0e9, 0.3);
        const Tet4Stiffness firstKe = stiffnessOf(nodes, 210.0e9, 0.3);
        for (int repeat = 0; repeat < 16; ++repeat) {
            INFO("repeat " << repeat);
            CHECK(kinematicsOf(nodes) == first);
            CHECK(elasticityOf(210.0e9, 0.3) == firstD);
            CHECK(stiffnessOf(nodes, 210.0e9, 0.3) == firstKe);
        }
    }
}

TEST_CASE("Tet4Element_HasNoDimensionalAmbiguity", "[structural][elem]") {
    // The dimensional reasoning of `Ke = V B^T D B`, which the header also
    // asserts at compile time. Repeated here so the claim is visible in the
    // test record and so a change to the quantity aliases breaks a test.
    STATIC_REQUIRE(structural::InverseLength::dimension == dimensions::length.inverse());
    STATIC_REQUIRE(structural::Stiffness::dimension ==
                   dimensions::force / dimensions::length);
    STATIC_REQUIRE(decltype(Volume{} * structural::InverseLength{} * Stress{} *
                            structural::InverseLength{})::dimension ==
                   structural::Stiffness::dimension);
    // And the physical reading: 1 N/m really is 1 N per metre.
    const Tet4Stiffness ke = stiffnessOf(unitTet(), 1.0, 0.0);
    const std::array<Force3D, kTet4Nodes> f =
        ke.forceFrom({move(0, 0, 0), move(1.0, 0, 0), move(0, 0, 0), move(0, 0, 0)});
    INFO("Ke(3,3) = " << ke(3, 3).si() << " N/m, f = " << f[1].x.si() << " N for u = 1 m");
    CHECK_THAT(f[1].x.si(), WithinRel(ke(3, 3).si(), 1e-15));
}
