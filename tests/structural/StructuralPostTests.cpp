// P17-POST-001: displacement, strain, stress and the derived invariants.
//
// THE SHARPEST TEST IN THIS FILE IS THE FULL-3D VON MISES FIXTURE, and the
// reason is that almost every other reasonable test passes with a plane-stress
// formula. A 2D von Mises drops `szz`, `tyz` and `tzx`, and it agrees with the
// correct answer on every uniaxial state, on every pure-XY-shear state and on
// any state where the out-of-plane terms happen to vanish. So the mandatory
// fixture has ALL THREE nonzero:
//
//     sxx 120, syy -35, szz 55, txy 18, tyz -27, tzx 41  (MPa)
//
// and it is checked against two oracles that share no code with production:
// the deviatoric invariant sqrt(3/2 s:s), and the principal-stress form.
//
// THE ORACLES ARE INDEPENDENT, NOT A SECOND CALL. The affine strain is
// differentiated by hand; the stress uses a test-side `Dref` built from
// lambda and mu written out in the test; the principal values of the full 3D
// state come from CARDANO'S closed form, which is a different algorithm from
// Eigen's tridiagonal QL rather than a second use of it. Asking production
// what the answer should be is the trap, and nothing here does it.
//
// THE AFFINE SINGLE-TET CASE ISOLATES POST-PROCESSING FROM THE SOLVER. A
// displacement field is written down, sampled at four tetrahedron corners and
// pushed through the same two functions `recoverFields` calls per element --
// `strainFrom` and `stressFrom` -- so a discrepancy cannot be blamed on the
// solve. The end-to-end case then proves the gather and the traversal that the
// synthetic case cannot reach.
//
// THE FIXTURE IS DUPLICATED FROM StructuralSolveTests.cpp DELIBERATELY, as
// P17-SOLVE-001 duplicated it from P17-ASSEMBLY-001. Extracting it to a shared
// header would edit three qualified test files and owe their requalification,
// for scaffolding rather than for a production reason.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <type_traits>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::ConstraintSet;
using structural::DofComponent;
using structural::DofIndex;
using structural::ElasticityMatrix;
using structural::ElementFields;
using structural::GlobalStructuralSystem;
using structural::kDofsPerNode;
using structural::kTet4Dofs;
using structural::kTet4Nodes;
using structural::MeshDofMap;
using structural::NodalDisplacement;
using structural::PreparedLoads;
using structural::PrincipalStrains;
using structural::PrincipalStresses;
using structural::RecoveredFields;
using structural::RecoveryProblem;
using structural::SolvedSystem;
using structural::SolverSettings;
using structural::Strain6;
using structural::StrainTensor3;
using structural::Stress6;
using structural::StressTensor3;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::StructuralRestraint;
using structural::Tet4Kinematics;

namespace {

constexpr double kYoungs = 210.0e9;
constexpr double kPoisson = 0.3;

/// Lame parameters, WRITTEN OUT IN THE TEST rather than read from production.
/// These are the independent constants every stress oracle below uses.
constexpr double kMu = kYoungs / (2.0 * (1.0 + kPoisson));
constexpr double kLambda = kYoungs * kPoisson / ((1.0 + kPoisson) * (1.0 - 2.0 * kPoisson));

// ---------------------------------------------------------------------------
// Independent oracles
// ---------------------------------------------------------------------------

/// `sigma = Dref eps`, a SECOND constitutive matrix written from the continuum
/// relations rather than obtained from `isotropicElasticity`.
///
/// ```text
///     sii = lambda tr(eps) + 2 mu eii
///     tij = mu gij                      (engineering shear)
/// ```
///
/// The `mu gij` rather than `2 mu gij` is the whole convention, and it is
/// asserted here from the physics rather than copied from the production
/// matrix.
[[nodiscard]] Stress6 referenceStress(const Strain6& e) {
    const double trace = e.xx + e.yy + e.zz;
    return Stress6{.xx = Stress::fromSi(kLambda * trace + 2.0 * kMu * e.xx),
                   .yy = Stress::fromSi(kLambda * trace + 2.0 * kMu * e.yy),
                   .zz = Stress::fromSi(kLambda * trace + 2.0 * kMu * e.zz),
                   .xy = Stress::fromSi(kMu * e.gammaXy),
                   .yz = Stress::fromSi(kMu * e.gammaYz),
                   .zx = Stress::fromSi(kMu * e.gammaZx)};
}

/// Von Mises through the DEVIATORIC invariant, which production does not use:
///
/// ```text
///     s        = S - tr(S)/3 I
///     sigma_vm = sqrt(3/2 s:s)
/// ```
///
/// `s:s` is the full double contraction, so each off-diagonal is counted
/// TWICE -- which is the independent route's own convention trap and is the
/// reason it is a real second opinion rather than a rearrangement.
[[nodiscard]] double deviatoricVonMises(const Stress6& s) {
    const double mean = (s.xx.si() + s.yy.si() + s.zz.si()) / 3.0;
    const double dxx = s.xx.si() - mean;
    const double dyy = s.yy.si() - mean;
    const double dzz = s.zz.si() - mean;
    const double contraction = dxx * dxx + dyy * dyy + dzz * dzz +
                               2.0 * (s.xy.si() * s.xy.si() + s.yz.si() * s.yz.si() +
                                      s.zx.si() * s.zx.si());
    return std::sqrt(1.5 * contraction);
}

/// Von Mises from three principal stresses.
[[nodiscard]] double principalVonMises(double s1, double s2, double s3) {
    return std::sqrt(((s1 - s2) * (s1 - s2) + (s2 - s3) * (s2 - s3) + (s3 - s1) * (s3 - s1)) / 2.0);
}

/// The three eigenvalues of a symmetric 3x3 by CARDANO'S trigonometric
/// solution, descending.
///
/// A DIFFERENT ALGORITHM FROM EIGEN'S, not a second call to it: this solves the
/// characteristic cubic in closed form through the invariants, where Eigen
/// reduces to tridiagonal form and iterates QL. So an agreement between the
/// two is evidence rather than a tautology.
[[nodiscard]] std::array<double, 3> cardanoEigenvalues(const std::array<double, 6>& voigt) {
    // xx yy zz xy yz zx, the same order as everything else here.
    const double a = voigt[0];
    const double b = voigt[1];
    const double c = voigt[2];
    const double d = voigt[3]; // xy
    const double e = voigt[4]; // yz
    const double f = voigt[5]; // zx

    const double trace = a + b + c;
    const double mean = trace / 3.0;
    // The deviator, whose eigenvalues are the shifted ones.
    const double p = a - mean;
    const double q = b - mean;
    const double r = c - mean;

    const double j2 = 0.5 * (p * p + q * q + r * r) + d * d + e * e + f * f;
    // det of the deviator.
    const double j3 = p * (q * r - e * e) - d * (d * r - e * f) + f * (d * e - q * f);

    if (j2 <= 0.0) {
        return {mean, mean, mean};
    }
    const double scale = std::sqrt(j2 / 3.0);
    double cosine = j3 / (2.0 * scale * scale * scale);
    cosine = std::clamp(cosine, -1.0, 1.0);
    const double angle = std::acos(cosine) / 3.0;
    const double twoPiThirds = 2.0 * std::numbers::pi / 3.0;

    std::array<double, 3> out{mean + 2.0 * scale * std::cos(angle),
                              mean + 2.0 * scale * std::cos(angle - twoPiThirds),
                              mean + 2.0 * scale * std::cos(angle + twoPiThirds)};
    std::ranges::sort(out, std::ranges::greater{});
    return out;
}

[[nodiscard]] Stress6 stressOf(double xx, double yy, double zz, double xy, double yz, double zx) {
    return Stress6{.xx = Stress::fromSi(xx),
                   .yy = Stress::fromSi(yy),
                   .zz = Stress::fromSi(zz),
                   .xy = Stress::fromSi(xy),
                   .yz = Stress::fromSi(yz),
                   .zx = Stress::fromSi(zx)};
}

[[nodiscard]] Strain6 strainOf(double xx, double yy, double zz, double gxy, double gyz,
                               double gzx) {
    return Strain6{
        .xx = xx, .yy = yy, .zz = zz, .gammaXy = gxy, .gammaYz = gyz, .gammaZx = gzx};
}

/// `S + q I`.
[[nodiscard]] Stress6 shifted(const Stress6& s, double q) {
    return stressOf(s.xx.si() + q, s.yy.si() + q, s.zz.si() + q, s.xy.si(), s.yz.si(), s.zx.si());
}

/// `S' = R S R^T` for a proper rotation `R`, returned in Voigt order.
///
/// THE STRESS COMPONENTS CHANGE AND THE INVARIANTS DO NOT, which is the claim.
/// The product is written out with explicit 3x3 arithmetic rather than through
/// a production tensor type, so the rotated state is an independent construction.
[[nodiscard]] Stress6 rotated(const Stress6& s, const std::array<std::array<double, 3>, 3>& r) {
    const std::array<std::array<double, 3>, 3> m{
        {{s.xx.si(), s.xy.si(), s.zx.si()},
         {s.xy.si(), s.yy.si(), s.yz.si()},
         {s.zx.si(), s.yz.si(), s.zz.si()}}};
    std::array<std::array<double, 3>, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                for (std::size_t l = 0; l < 3; ++l) {
                    sum += r[i][k] * m[k][l] * r[j][l];
                }
            }
            out[i][j] = sum;
        }
    }
    return stressOf(out[0][0], out[1][1], out[2][2], out[0][1], out[1][2], out[2][0]);
}

/// A proper rotation about an axis, built from the axis-angle form.
[[nodiscard]] std::array<std::array<double, 3>, 3> rotation(double ax, double ay, double az,
                                                           double angle) {
    const double norm = std::hypot(ax, ay, az);
    const double x = ax / norm;
    const double y = ay / norm;
    const double z = az / norm;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    return {{{t * x * x + c, t * x * y - s * z, t * x * z + s * y},
             {t * x * y + s * z, t * y * y + c, t * y * z - s * x},
             {t * x * z - s * y, t * y * z + s * x, t * z * z + c}}};
}

// ---------------------------------------------------------------------------
// A single tetrahedron with a written-down displacement field
// ---------------------------------------------------------------------------

/// Four corners of a valid, positively oriented tetrahedron, in metres.
///
/// NOT THE UNIT TET. Deliberately irregular, so that a `B` whose rows were
/// permuted or whose gradients were transposed could not survive by symmetry,
/// and offset from the origin so that nothing can depend on a corner being at
/// zero.
[[nodiscard]] std::array<Point3D, kTet4Nodes> irregularTet() {
    return {Point3D{Length::fromSi(0.011), Length::fromSi(-0.004), Length::fromSi(0.021)},
            Point3D{Length::fromSi(0.034), Length::fromSi(0.002), Length::fromSi(0.019)},
            Point3D{Length::fromSi(0.015), Length::fromSi(0.027), Length::fromSi(0.023)},
            Point3D{Length::fromSi(0.018), Length::fromSi(0.001), Length::fromSi(0.046)}};
}

/// The coefficients of an affine displacement field.
///
/// ```text
///     ux = a0 + a1 x + a2 y + a3 z
///     uy = b0 + b1 x + b2 y + b3 z
///     uz = c0 + c1 x + c2 y + c3 z
/// ```
struct Affine {
    std::array<double, 4> a{};
    std::array<double, 4> b{};
    std::array<double, 4> c{};

    [[nodiscard]] Translation3D at(const Point3D& p) const {
        const double x = p.x.si();
        const double y = p.y.si();
        const double z = p.z.si();
        return Translation3D{Length::fromSi(a[0] + a[1] * x + a[2] * y + a[3] * z),
                             Length::fromSi(b[0] + b[1] * x + b[2] * y + b[3] * z),
                             Length::fromSi(c[0] + c[1] * x + c[2] * y + c[3] * z)};
    }

    /// The strain of this field, BY HAND from the analytic derivatives:
    ///
    /// ```text
    ///     exx = a1      eyy = b2      ezz = c3
    ///     gxy = a2 + b1
    ///     gyz = b3 + c2
    ///     gzx = c1 + a3
    /// ```
    ///
    /// Independent of production: nothing here calls `B`.
    [[nodiscard]] Strain6 analyticStrain() const {
        return strainOf(a[1], b[2], c[3], a[2] + b[1], b[3] + c[2], c[1] + a[3]);
    }

    [[nodiscard]] std::array<Translation3D, kTet4Nodes>
    sample(const std::array<Point3D, kTet4Nodes>& nodes) const {
        return {at(nodes[0]), at(nodes[1]), at(nodes[2]), at(nodes[3])};
    }
};

[[nodiscard]] Tet4Kinematics kinematicsOf(const std::array<Point3D, kTet4Nodes>& nodes) {
    Result<Tet4Kinematics> kinematics = structural::computeTet4Kinematics(nodes);
    INFO((kinematics.has_value() ? std::string{} : kinematics.error().message));
    REQUIRE(kinematics.has_value());
    return *kinematics;
}

[[nodiscard]] ElasticityMatrix steelElasticity() {
    materials::LinearElasticConstants constants{};
    constants.youngsModulus = ElasticModulus::fromSi(kYoungs);
    constants.poissonRatio = PoissonRatio::of(kPoisson);
    constants.shearModulus = ElasticModulus::fromSi(kMu);
    constants.bulkModulus =
        ElasticModulus::fromSi(kYoungs / (3.0 * (1.0 - 2.0 * kPoisson)));
    Result<ElasticityMatrix> d = structural::isotropicElasticity(constants);
    INFO((d.has_value() ? std::string{} : d.error().message));
    REQUIRE(d.has_value());
    return *d;
}

[[nodiscard]] PrincipalStresses principalsOf(const Stress6& s) {
    Result<PrincipalStresses> p = structural::principalStresses(s);
    INFO((p.has_value() ? std::string{} : p.error().message));
    REQUIRE(p.has_value());
    return *p;
}

[[nodiscard]] PrincipalStrains principalsOf(const Strain6& e) {
    Result<PrincipalStrains> p = structural::principalStrains(e);
    INFO((p.has_value() ? std::string{} : p.error().message));
    REQUIRE(p.has_value());
    return *p;
}

} // namespace

// ---------------------------------------------------------------------------
// Displacement magnitude
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_ComputesDisplacementMagnitudeAsAEuclideanNorm",
          "[structural][post]") {
    const auto magnitude = [](double x, double y, double z) {
        return structural::displacementMagnitude(
                   Translation3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)})
            .si();
    };

    SECTION("the 3-4-5 case, exactly") {
        REQUIRE_THAT(magnitude(3.0, 4.0, 0.0), WithinRel(5.0, 1e-15));
        REQUIRE_THAT(magnitude(0.0, 3.0, 4.0), WithinRel(5.0, 1e-15));
        REQUIRE_THAT(magnitude(4.0, 0.0, 3.0), WithinRel(5.0, 1e-15));
        // Sign cannot matter: a magnitude is a magnitude.
        REQUIRE_THAT(magnitude(-3.0, -4.0, 0.0), WithinRel(5.0, 1e-15));
    }

    SECTION("zero is zero") { REQUIRE(magnitude(0.0, 0.0, 0.0) == 0.0); }

    SECTION("a non-axis-aligned case, which is what separates it from the alternatives") {
        // THE DISCRIMINATING CASE. For an axis-aligned vector the Euclidean
        // norm, the component maximum and |ux+uy+uz| all agree, so none of
        // them is tested by one. Here they do not:
        //
        //     sqrt(1+4+9) = 3.741657...      the norm
        //     3                              the component maximum
        //     6                              |sum of components|
        //     sqrt(14)/... etc
        const double expected = std::sqrt(14.0);
        REQUIRE_THAT(magnitude(1.0, 2.0, 3.0), WithinRel(expected, 1e-15));
        REQUIRE(expected != 3.0);
        REQUIRE(expected != 6.0);
        // And with mixed signs the sum would be 0 while the norm is not.
        REQUIRE_THAT(magnitude(1.0, 2.0, -3.0), WithinRel(expected, 1e-15));
    }

    SECTION("engineering magnitudes, and the result is a Length in metres") {
        const Length m = structural::displacementMagnitude(
            Translation3D{Length::fromSi(3.0e-6), Length::fromSi(4.0e-6), Length::fromSi(0.0)});
        REQUIRE_THAT(m.si(), WithinRel(5.0e-6, 1e-15));
        // Not millimetres: the stored number is SI.
        REQUIRE(m.si() < 1.0e-5);
    }

    SECTION("hypot does not overflow where a naive sum of squares would") {
        // 1e200 squared overflows a double; hypot does not form it.
        const double big = structural::displacementMagnitude(
            Translation3D{Length::fromSi(3.0e200), Length::fromSi(4.0e200),
                          Length::fromSi(0.0)}).si();
        REQUIRE(std::isfinite(big));
        REQUIRE_THAT(big, WithinRel(5.0e200, 1e-14));
    }
}

// ---------------------------------------------------------------------------
// Voigt to tensor
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_MapsVoigtStressOntoTheSymmetricTensorPositions",
          "[structural][post]") {
    // Six distinct values, so a transposed or swapped component is visible.
    const Stress6 s = stressOf(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
    const StressTensor3 t = structural::tensorOf(s);

    SECTION("the diagonal") {
        REQUIRE(t.at(0, 0).si() == 1.0);
        REQUIRE(t.at(1, 1).si() == 2.0);
        REQUIRE(t.at(2, 2).si() == 3.0);
    }

    SECTION("XY is (0,1), YZ is (1,2), and ZX is (2,0)") {
        // THE SIXTH COMPONENT IS THE ONE THAT GETS MISPLACED. ZX belongs at
        // (2,0) and (0,2); putting YZ there instead would make at(0,2) read 5.
        REQUIRE(t.at(0, 1).si() == 4.0);
        REQUIRE(t.at(1, 2).si() == 5.0);
        REQUIRE(t.at(2, 0).si() == 6.0);
        REQUIRE(t.at(0, 2).si() == 6.0);
        REQUIRE(t.at(0, 2).si() != 5.0);
    }

    SECTION("symmetric in every off-diagonal position") {
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                REQUIRE(t.at(i, j).si() == t.at(j, i).si());
            }
        }
    }

    SECTION("out of range is zero, the convention B and D already set") {
        REQUIRE(t.at(3, 0).si() == 0.0);
        REQUIRE(t.at(0, 3).si() == 0.0);
    }

    SECTION("the trace is the first invariant") {
        REQUIRE_THAT(t.trace().si(), WithinRel(6.0, 1e-15));
    }

    SECTION("no factor is applied anywhere: it is a relabelling") {
        REQUIRE(t.xx == s.xx);
        REQUIRE(t.xy == s.xy);
        REQUIRE(t.yz == s.yz);
        REQUIRE(t.zx == s.zx);
    }
}

TEST_CASE("StructuralPost_HalvesEngineeringShearWhenBuildingTheStrainTensor",
          "[structural][post]") {
    const Strain6 e = strainOf(1.0e-4, 2.0e-4, 3.0e-4, 8.0e-4, 1.0e-3, 1.2e-3);
    const StrainTensor3 t = structural::tensorOf(e);

    SECTION("the normal components are untouched") {
        REQUIRE(t.at(0, 0) == 1.0e-4);
        REQUIRE(t.at(1, 1) == 2.0e-4);
        REQUIRE(t.at(2, 2) == 3.0e-4);
    }

    SECTION("every off-diagonal is gamma / 2, not gamma") {
        // THE CONVENTION TEST. Storing the full engineering shear here doubles
        // every shear term of every principal strain, and the normal terms stay
        // right -- so only this assertion and the pure-shear principal case
        // can see it.
        REQUIRE_THAT(t.at(0, 1), WithinRel(4.0e-4, 1e-15));
        REQUIRE_THAT(t.at(1, 2), WithinRel(5.0e-4, 1e-15));
        REQUIRE_THAT(t.at(2, 0), WithinRel(6.0e-4, 1e-15));
        // And explicitly NOT the engineering value.
        REQUIRE(t.at(0, 1) != e.gammaXy);
        REQUIRE(t.at(1, 2) != e.gammaYz);
        REQUIRE(t.at(2, 0) != e.gammaZx);
    }

    SECTION("symmetric, and ZX is at (2,0)") {
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                REQUIRE(t.at(i, j) == t.at(j, i));
            }
        }
        REQUIRE_THAT(t.at(0, 2), WithinRel(6.0e-4, 1e-15));
    }

    SECTION("the trace is the volumetric strain and carries no shear") {
        REQUIRE_THAT(t.trace(), WithinRel(6.0e-4, 1e-15));
    }
}

// ---------------------------------------------------------------------------
// Strain recovery: the affine field
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_RecoversAffineStrainFromASingleTetrahedron",
          "[structural][post]") {
    // MANDATORY, AND IT ISOLATES POST-PROCESSING FROM THE SOLVER. The field is
    // written down, sampled at the four corners, and pushed through the same
    // `strainFrom` the production traversal calls. The expected values are the
    // analytic derivatives.
    const Affine field{.a = {1.0e-6, 3.0e-4, -7.0e-4, 2.0e-4},
                       .b = {-2.0e-6, 5.0e-4, 1.1e-3, -4.0e-4},
                       .c = {4.0e-6, -6.0e-4, 9.0e-4, 1.3e-3}};
    const std::array<Point3D, kTet4Nodes> nodes = irregularTet();
    const Tet4Kinematics kinematics = kinematicsOf(nodes);

    const Strain6 recovered = kinematics.strainFrom(field.sample(nodes));
    const Strain6 expected = field.analyticStrain();

    SECTION("every one of the six components, against the analytic derivative") {
        REQUIRE_THAT(recovered.xx, WithinRel(expected.xx, 1e-10));
        REQUIRE_THAT(recovered.yy, WithinRel(expected.yy, 1e-10));
        REQUIRE_THAT(recovered.zz, WithinRel(expected.zz, 1e-10));
        REQUIRE_THAT(recovered.gammaXy, WithinRel(expected.gammaXy, 1e-10));
        REQUIRE_THAT(recovered.gammaYz, WithinRel(expected.gammaYz, 1e-10));
        REQUIRE_THAT(recovered.gammaZx, WithinRel(expected.gammaZx, 1e-10));
    }

    SECTION("the six expected values are distinct, so a permutation cannot pass") {
        // A VACUOUS-INSTRUMENT GUARD. If two expected components were equal, a
        // swap between them would be invisible. They are not.
        std::array<double, 6> values{expected.xx,      expected.yy,      expected.zz,
                                     expected.gammaXy, expected.gammaYz, expected.gammaZx};
        std::ranges::sort(values);
        REQUIRE(std::ranges::adjacent_find(values) == values.end());
    }

    SECTION("the stress follows, against an independent Dref") {
        const Stress6 recoveredStress = steelElasticity().stressFrom(recovered);
        const Stress6 expectedStress = referenceStress(expected);
        REQUIRE_THAT(recoveredStress.xx.si(), WithinRel(expectedStress.xx.si(), 1e-10));
        REQUIRE_THAT(recoveredStress.yy.si(), WithinRel(expectedStress.yy.si(), 1e-10));
        REQUIRE_THAT(recoveredStress.zz.si(), WithinRel(expectedStress.zz.si(), 1e-10));
        REQUIRE_THAT(recoveredStress.xy.si(), WithinRel(expectedStress.xy.si(), 1e-10));
        REQUIRE_THAT(recoveredStress.yz.si(), WithinRel(expectedStress.yz.si(), 1e-10));
        REQUIRE_THAT(recoveredStress.zx.si(), WithinRel(expectedStress.zx.si(), 1e-10));
    }

    SECTION("and it does not depend on where the tetrahedron sits") {
        // TRANSLATION INVARIANCE of the recovery: the same field gradient
        // sampled on a tetrahedron moved bodily gives the same strain, because
        // strain is a derivative. The constant terms of the field change what
        // the samples ARE, so the field is re-sampled rather than reused.
        std::array<Point3D, kTet4Nodes> moved = nodes;
        for (Point3D& p : moved) {
            p.x = p.x + Length::fromSi(0.5);
            p.y = p.y + Length::fromSi(-0.25);
            p.z = p.z + Length::fromSi(1.5);
        }
        const Strain6 there = kinematicsOf(moved).strainFrom(field.sample(moved));
        REQUIRE_THAT(there.xx, WithinRel(expected.xx, 1e-9));
        REQUIRE_THAT(there.gammaXy, WithinRel(expected.gammaXy, 1e-9));
        REQUIRE_THAT(there.gammaYz, WithinRel(expected.gammaYz, 1e-9));
        REQUIRE_THAT(there.gammaZx, WithinRel(expected.gammaZx, 1e-9));
    }
}

TEST_CASE("StructuralPost_RecoversZeroStrainFromARigidMotion", "[structural][post]") {
    const std::array<Point3D, kTet4Nodes> nodes = irregularTet();
    const Tet4Kinematics kinematics = kinematicsOf(nodes);

    SECTION("a constant translation") {
        const Translation3D t{Length::fromSi(1.3e-3), Length::fromSi(-7.0e-4),
                              Length::fromSi(2.9e-3)};
        const Strain6 e = kinematics.strainFrom({t, t, t, t});
        // The tetrahedron is ~2e-2 m across and the translation ~1e-3 m, so a
        // spurious gradient would land near 1e-1. Zero to 1e-16 is a genuine
        // cancellation rather than a loose bound.
        REQUIRE_THAT(e.xx, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.yy, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.zz, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.gammaXy, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.gammaYz, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.gammaZx, WithinAbs(0.0, 1e-16));
    }

    SECTION("an infinitesimal rotation u = omega x r") {
        // THE STRONGER OF THE TWO. A translation is killed by any B whose rows
        // sum to zero; a rotation is killed only by one whose SYMMETRIC part is
        // right, so this is what proves the engineering-shear pairing.
        const std::array<double, 3> omega{7.0e-4, -3.0e-4, 5.0e-4};
        std::array<Translation3D, kTet4Nodes> u{};
        for (std::size_t i = 0; i < kTet4Nodes; ++i) {
            const double x = nodes[i].x.si();
            const double y = nodes[i].y.si();
            const double z = nodes[i].z.si();
            u[i] = Translation3D{Length::fromSi(omega[1] * z - omega[2] * y),
                                 Length::fromSi(omega[2] * x - omega[0] * z),
                                 Length::fromSi(omega[0] * y - omega[1] * x)};
        }
        const Strain6 e = kinematics.strainFrom(u);
        REQUIRE_THAT(e.xx, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.yy, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.zz, WithinAbs(0.0, 1e-16));
        // The shear terms are where a missing or doubled factor would show.
        REQUIRE_THAT(e.gammaXy, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.gammaYz, WithinAbs(0.0, 1e-16));
        REQUIRE_THAT(e.gammaZx, WithinAbs(0.0, 1e-16));

        // AND THE INSTRUMENT IS NOT VACUOUS: the same displacements with one
        // component's sign flipped are NOT a rigid rotation, and the strain is
        // then large. Without this the test above would pass on `return 0`.
        std::array<Translation3D, kTet4Nodes> bent = u;
        bent[2].y = -bent[2].y;
        const Strain6 notRigid = kinematics.strainFrom(bent);
        REQUIRE(std::abs(notRigid.gammaXy) > 1e-6);
    }
}

TEST_CASE("StructuralPost_RecoversEachPureStrainComponentSeparately", "[structural][post]") {
    const std::array<Point3D, kTet4Nodes> nodes = irregularTet();
    const Tet4Kinematics kinematics = kinematicsOf(nodes);
    constexpr double kE = 2.5e-4;

    // ONE COMPONENT AT A TIME, which is what catches a permuted row: a field
    // whose only gradient is du_x/dx must give exx and five zeros.
    struct Case {
        Affine field;
        Strain6 expected;
        const char* name;
    };
    const std::vector<Case> cases{
        {{.a = {0.0, kE, 0.0, 0.0}}, strainOf(kE, 0, 0, 0, 0, 0), "pure exx"},
        {{.b = {0.0, 0.0, kE, 0.0}}, strainOf(0, kE, 0, 0, 0, 0), "pure eyy"},
        {{.c = {0.0, 0.0, 0.0, kE}}, strainOf(0, 0, kE, 0, 0, 0), "pure ezz"},
        // ENGINEERING SHEAR: du_x/dy = kE alone gives gammaXy = kE, because
        // gamma = du_x/dy + du_y/dx. A tensor-shear convention would give kE/2.
        {{.a = {0.0, 0.0, kE, 0.0}}, strainOf(0, 0, 0, kE, 0, 0), "pure gxy from du_x/dy"},
        {{.b = {0.0, kE, 0.0, 0.0}}, strainOf(0, 0, 0, kE, 0, 0), "pure gxy from du_y/dx"},
        {{.b = {0.0, 0.0, 0.0, kE}}, strainOf(0, 0, 0, 0, kE, 0), "pure gyz from du_y/dz"},
        {{.c = {0.0, 0.0, kE, 0.0}}, strainOf(0, 0, 0, 0, kE, 0), "pure gyz from du_z/dy"},
        {{.c = {0.0, kE, 0.0, 0.0}}, strainOf(0, 0, 0, 0, 0, kE), "pure gzx from du_z/dx"},
        {{.a = {0.0, 0.0, 0.0, kE}}, strainOf(0, 0, 0, 0, 0, kE), "pure gzx from du_x/dz"},
    };

    for (const Case& c : cases) {
        INFO(c.name);
        const Strain6 e = kinematics.strainFrom(c.field.sample(nodes));
        // The analytic derivative of the field must agree with the hand-written
        // expectation first -- otherwise the case itself is wrong.
        const Strain6 analytic = c.field.analyticStrain();
        REQUIRE(analytic == c.expected);

        REQUIRE_THAT(e.xx, WithinAbs(c.expected.xx, 1e-14));
        REQUIRE_THAT(e.yy, WithinAbs(c.expected.yy, 1e-14));
        REQUIRE_THAT(e.zz, WithinAbs(c.expected.zz, 1e-14));
        REQUIRE_THAT(e.gammaXy, WithinAbs(c.expected.gammaXy, 1e-14));
        REQUIRE_THAT(e.gammaYz, WithinAbs(c.expected.gammaYz, 1e-14));
        REQUIRE_THAT(e.gammaZx, WithinAbs(c.expected.gammaZx, 1e-14));
    }
}

// ---------------------------------------------------------------------------
// Stress recovery
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_ConvertsPureEngineeringShearThroughMuAndNotTwoMu",
          "[structural][post]") {
    const ElasticityMatrix d = steelElasticity();
    constexpr double kGamma = 1.5e-4;

    // tau = mu gamma. The other convention gives 2 mu gamma, leaves every
    // normal term correct, and is the hardest defect in this milestone to see.
    SECTION("gamma_xy alone gives tau_xy = mu gamma and nothing else") {
        const Stress6 s = d.stressFrom(strainOf(0, 0, 0, kGamma, 0, 0));
        REQUIRE_THAT(s.xy.si(), WithinRel(kMu * kGamma, 1e-12));
        REQUIRE(s.xy.si() != 2.0 * kMu * kGamma);
        REQUIRE_THAT(s.xx.si(), WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(s.yy.si(), WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(s.zz.si(), WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(s.yz.si(), WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(s.zx.si(), WithinAbs(0.0, 1e-6));
    }

    SECTION("gamma_yz alone") {
        const Stress6 s = d.stressFrom(strainOf(0, 0, 0, 0, kGamma, 0));
        REQUIRE_THAT(s.yz.si(), WithinRel(kMu * kGamma, 1e-12));
        REQUIRE_THAT(s.xy.si(), WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(s.zx.si(), WithinAbs(0.0, 1e-6));
    }

    SECTION("gamma_zx alone, which freezes the sixth row") {
        const Stress6 s = d.stressFrom(strainOf(0, 0, 0, 0, 0, kGamma));
        REQUIRE_THAT(s.zx.si(), WithinRel(kMu * kGamma, 1e-12));
        REQUIRE_THAT(s.xy.si(), WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(s.yz.si(), WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("StructuralPost_ConvertsUniaxialStrainThroughLambdaAndMu", "[structural][post]") {
    const ElasticityMatrix d = steelElasticity();
    constexpr double kE = 3.0e-4;
    const Stress6 s = d.stressFrom(strainOf(kE, 0, 0, 0, 0, 0));

    // UNIAXIAL STRAIN, NOT UNIAXIAL STRESS. Holding eyy = ezz = 0 requires
    // lateral stress, so syy and szz are NOT zero -- which is the point.
    REQUIRE_THAT(s.xx.si(), WithinRel((kLambda + 2.0 * kMu) * kE, 1e-12));
    REQUIRE_THAT(s.yy.si(), WithinRel(kLambda * kE, 1e-12));
    REQUIRE_THAT(s.zz.si(), WithinRel(kLambda * kE, 1e-12));
    REQUIRE_THAT(s.xy.si(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s.yz.si(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s.zx.si(), WithinAbs(0.0, 1e-6));
    // And the three normal values are distinct, so the assertion is not vacuous.
    REQUIRE(s.xx.si() != s.yy.si());
    REQUIRE(s.yy.si() > 0.0);
}

TEST_CASE("StructuralPost_ConvertsHydrostaticStrainToAHydrostaticStress",
          "[structural][post]") {
    const ElasticityMatrix d = steelElasticity();
    constexpr double kE = -2.0e-4; // compressive
    const Stress6 s = d.stressFrom(strainOf(kE, kE, kE, 0, 0, 0));

    const double expected = (3.0 * kLambda + 2.0 * kMu) * kE;
    REQUIRE_THAT(s.xx.si(), WithinRel(expected, 1e-12));
    REQUIRE_THAT(s.yy.si(), WithinRel(expected, 1e-12));
    REQUIRE_THAT(s.zz.si(), WithinRel(expected, 1e-12));
    REQUIRE_THAT(s.xy.si(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s.yz.si(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s.zx.si(), WithinAbs(0.0, 1e-6));
    REQUIRE(expected < 0.0);

    SECTION("so von Mises is zero and the hydrostatic stress is the value itself") {
        REQUIRE_THAT(structural::vonMisesStress(s).si(), WithinAbs(0.0, 1e-3));
        REQUIRE_THAT(structural::hydrostaticStress(s).si(), WithinRel(expected, 1e-12));
    }
}

// ---------------------------------------------------------------------------
// Principal stresses
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_OrdersPrincipalStressesDescending", "[structural][post]") {
    SECTION("a diagonal state, given in ascending order, comes back descending") {
        // GIVEN ASCENDING ON PURPOSE. If the implementation passed the
        // eigensolver's own order through, Eigen documents ascending, so this
        // would come back 10, 20, 30 and fail.
        const PrincipalStresses p = principalsOf(stressOf(10.0e6, 20.0e6, 30.0e6, 0, 0, 0));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(30.0e6, 1e-12));
        REQUIRE_THAT(p.sigma2.si(), WithinRel(20.0e6, 1e-12));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(10.0e6, 1e-12));
    }

    SECTION("a mixed-sign state with shear") {
        const Stress6 s = stressOf(100.0e6, 20.0e6, -50.0e6, 15.0e6, 0.0, 0.0);
        const PrincipalStresses p = principalsOf(s);
        REQUIRE(p.sigma1.si() >= p.sigma2.si());
        REQUIRE(p.sigma2.si() >= p.sigma3.si());
        // Against Cardano, independently.
        const std::array<double, 3> oracle =
            cardanoEigenvalues({100.0e6, 20.0e6, -50.0e6, 15.0e6, 0.0, 0.0});
        REQUIRE_THAT(p.sigma1.si(), WithinRel(oracle[0], 1e-10));
        REQUIRE_THAT(p.sigma2.si(), WithinRel(oracle[1], 1e-10));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(oracle[2], 1e-10));
        // The three are distinct, so the ordering claim is not vacuous.
        REQUIRE(p.sigma1.si() > p.sigma2.si());
        REQUIRE(p.sigma2.si() > p.sigma3.si());
    }

    SECTION("the trace identity holds: sum of principals == tr(S)") {
        const Stress6 s = stressOf(100.0e6, -50.0e6, 20.0e6, 15.0e6, -8.0e6, 11.0e6);
        const PrincipalStresses p = principalsOf(s);
        const double trace = structural::tensorOf(s).trace().si();
        REQUIRE_THAT(p.sum().si(), WithinRel(trace, 1e-12));
        // And the hydrostatic stress is that trace over three.
        REQUIRE_THAT(structural::hydrostaticStress(s).si(), WithinRel(trace / 3.0, 1e-12));
    }
}

TEST_CASE("StructuralPost_ReportsRepeatedPrincipalStressesForDegenerateStates",
          "[structural][post]") {
    SECTION("hydrostatic: a triple eigenvalue, and no shear is invented") {
        constexpr double kP = 75.0e6;
        const PrincipalStresses p = principalsOf(stressOf(kP, kP, kP, 0, 0, 0));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(kP, 1e-12));
        REQUIRE_THAT(p.sigma2.si(), WithinRel(kP, 1e-12));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(kP, 1e-12));
    }

    SECTION("hydrostatic compression keeps its sign") {
        constexpr double kP = -100.0e6;
        const PrincipalStresses p = principalsOf(stressOf(kP, kP, kP, 0, 0, 0));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(kP, 1e-12));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(kP, 1e-12));
        REQUIRE(p.sigma1.si() < 0.0);
        REQUIRE_THAT(structural::hydrostaticStress(stressOf(kP, kP, kP, 0, 0, 0)).si(),
                     WithinRel(kP, 1e-12));
    }

    SECTION("axisymmetric: a repeated pair and one distinct value") {
        const PrincipalStresses p = principalsOf(stressOf(40.0e6, 40.0e6, 90.0e6, 0, 0, 0));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(90.0e6, 1e-12));
        REQUIRE_THAT(p.sigma2.si(), WithinRel(40.0e6, 1e-12));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(40.0e6, 1e-12));
    }

    SECTION("a zero state stays finite and zero") {
        const PrincipalStresses p = principalsOf(stressOf(0, 0, 0, 0, 0, 0));
        REQUIRE(p.sigma1.si() == 0.0);
        REQUIRE(p.sigma2.si() == 0.0);
        REQUIRE(p.sigma3.si() == 0.0);
        REQUIRE(structural::vonMisesStress(stressOf(0, 0, 0, 0, 0, 0)).si() == 0.0);
    }
}

TEST_CASE("StructuralPost_ReportsPrincipalStressesOfAPureShearState", "[structural][post]") {
    constexpr double kTau = 60.0e6;

    // PURE SHEAR HAS PRINCIPAL STRESSES +|tau|, 0, -|tau|. This is the case
    // that proves the off-diagonal reached the tensor at full value: if the
    // mapping halved it, the answer would be tau/2.
    SECTION("tau_xy") {
        const PrincipalStresses p = principalsOf(stressOf(0, 0, 0, kTau, 0, 0));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(kTau, 1e-10));
        REQUIRE_THAT(p.sigma2.si(), WithinAbs(0.0, 1e-3));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(-kTau, 1e-10));
    }

    SECTION("tau_yz") {
        const PrincipalStresses p = principalsOf(stressOf(0, 0, 0, 0, kTau, 0));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(kTau, 1e-10));
        REQUIRE_THAT(p.sigma2.si(), WithinAbs(0.0, 1e-3));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(-kTau, 1e-10));
    }

    SECTION("tau_zx, which a misplaced sixth component would break") {
        const PrincipalStresses p = principalsOf(stressOf(0, 0, 0, 0, 0, kTau));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(kTau, 1e-10));
        REQUIRE_THAT(p.sigma2.si(), WithinAbs(0.0, 1e-3));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(-kTau, 1e-10));
    }

    SECTION("a negative shear gives the same pair, since the sign is in the axes") {
        const PrincipalStresses p = principalsOf(stressOf(0, 0, 0, -kTau, 0, 0));
        REQUIRE_THAT(p.sigma1.si(), WithinRel(kTau, 1e-10));
        REQUIRE_THAT(p.sigma3.si(), WithinRel(-kTau, 1e-10));
    }
}

TEST_CASE("StructuralPost_ShiftsEveryPrincipalStressByAHydrostaticShift",
          "[structural][post]") {
    const Stress6 base = stressOf(120.0e6, -35.0e6, 55.0e6, 18.0e6, -27.0e6, 41.0e6);
    const PrincipalStresses before = principalsOf(base);
    constexpr double kShift = 250.0e6;
    const PrincipalStresses after = principalsOf(shifted(base, kShift));

    // S + qI has eigenvalues sigma_i + q with the eigenvectors unchanged. It
    // validates the tensor assembly and the eigensolve at once: a mislaid
    // off-diagonal would change the eigenvalues in a way no constant shift
    // could reproduce.
    REQUIRE_THAT(after.sigma1.si(), WithinRel(before.sigma1.si() + kShift, 1e-11));
    REQUIRE_THAT(after.sigma2.si(), WithinRel(before.sigma2.si() + kShift, 1e-11));
    REQUIRE_THAT(after.sigma3.si(), WithinRel(before.sigma3.si() + kShift, 1e-11));

    // And the ordering survives the shift rather than being re-derived from it.
    REQUIRE(after.sigma1.si() >= after.sigma2.si());
    REQUIRE(after.sigma2.si() >= after.sigma3.si());
}

TEST_CASE("StructuralPost_KeepsTheInvariantsUnderARotationOfTheStressTensor",
          "[structural][post]") {
    const Stress6 base = stressOf(120.0e6, -35.0e6, 55.0e6, 18.0e6, -27.0e6, 41.0e6);
    const Stress6 turned = rotated(base, rotation(0.3, -0.7, 0.65, 0.9));

    SECTION("the raw components DO change -- otherwise the test proves nothing") {
        // A VACUOUS-INSTRUMENT GUARD. If the rotation were the identity, every
        // invariance assertion below would hold trivially.
        REQUIRE(std::abs(turned.xx.si() - base.xx.si()) > 1.0e6);
        REQUIRE(std::abs(turned.yz.si() - base.yz.si()) > 1.0e6);
    }

    SECTION("the principal stresses do not") {
        const PrincipalStresses before = principalsOf(base);
        const PrincipalStresses after = principalsOf(turned);
        REQUIRE_THAT(after.sigma1.si(), WithinRel(before.sigma1.si(), 1e-10));
        REQUIRE_THAT(after.sigma2.si(), WithinRel(before.sigma2.si(), 1e-10));
        REQUIRE_THAT(after.sigma3.si(), WithinRel(before.sigma3.si(), 1e-10));
    }

    SECTION("nor von Mises, nor the hydrostatic stress") {
        REQUIRE_THAT(structural::vonMisesStress(turned).si(),
                     WithinRel(structural::vonMisesStress(base).si(), 1e-10));
        REQUIRE_THAT(structural::hydrostaticStress(turned).si(),
                     WithinRel(structural::hydrostaticStress(base).si(), 1e-10));
    }

    SECTION("and the trace, which is the first invariant") {
        REQUIRE_THAT(structural::tensorOf(turned).trace().si(),
                     WithinRel(structural::tensorOf(base).trace().si(), 1e-10));
    }
}

// ---------------------------------------------------------------------------
// Von Mises
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_ComputesVonMisesFromTheFullThreeDimensionalState",
          "[structural][post]") {
    // THE MANDATORY FULL-3D FIXTURE, AND THE MOST IMPORTANT CASE IN THIS FILE.
    // szz, tyz and tzx are ALL nonzero, which is exactly what a plane-stress
    // formula omits -- and a plane-stress formula agrees with the correct one
    // on every uniaxial and pure-XY-shear state, so nothing else here catches
    // it.
    const Stress6 s = stressOf(120.0e6, -35.0e6, 55.0e6, 18.0e6, -27.0e6, 41.0e6);
    REQUIRE(s.zz.si() != 0.0);
    REQUIRE(s.yz.si() != 0.0);
    REQUIRE(s.zx.si() != 0.0);

    const double production = structural::vonMisesStress(s).si();

    SECTION("against the deviatoric invariant sqrt(3/2 s:s)") {
        REQUIRE_THAT(production, WithinRel(deviatoricVonMises(s), 1e-12));
    }

    SECTION("against the principal-stress form") {
        const PrincipalStresses p = principalsOf(s);
        REQUIRE_THAT(production,
                     WithinRel(principalVonMises(p.sigma1.si(), p.sigma2.si(), p.sigma3.si()),
                               1e-10));
    }

    SECTION("against Cardano's principal values, which never touch Eigen") {
        const std::array<double, 3> oracle =
            cardanoEigenvalues({120.0e6, -35.0e6, 55.0e6, 18.0e6, -27.0e6, 41.0e6});
        REQUIRE_THAT(production,
                     WithinRel(principalVonMises(oracle[0], oracle[1], oracle[2]), 1e-10));
    }

    SECTION("and a plane-stress formula gives a DIFFERENT answer") {
        // THE PROOF THAT THE FIXTURE DISCRIMINATES. The common 2D form is
        // computed here and must not match; without this assertion the fixture
        // could pass a 2D implementation by coincidence.
        const double sxx = s.xx.si();
        const double syy = s.yy.si();
        const double txy = s.xy.si();
        const double planeStress =
            std::sqrt(sxx * sxx - sxx * syy + syy * syy + 3.0 * txy * txy);
        INFO("full 3D " << production << " vs plane stress " << planeStress);
        REQUIRE(std::abs(production - planeStress) / production > 0.05);
        WARN("full-3D vm | production " << production << " | deviatoric "
                                        << deviatoricVonMises(s) << " | plane stress "
                                        << planeStress << " | relative difference "
                                        << std::abs(production - planeStress) / production);
    }
}

TEST_CASE("StructuralPost_ReportsZeroVonMisesForAnyHydrostaticState", "[structural][post]") {
    // CATCHES AN INCORRECT INCLUSION OF HYDROSTATIC STRESS: any formula that
    // let the mean stress through gives a large number here.
    for (const double p : {0.0, 1.0e3, -100.0e6, 500.0e6, -1.0e9}) {
        INFO("p = " << p);
        const Stress6 s = stressOf(p, p, p, 0, 0, 0);
        REQUIRE_THAT(structural::vonMisesStress(s).si(), WithinAbs(0.0, 1e-3));
        REQUIRE(structural::vonMisesStress(s).si() >= 0.0);
        if (p != 0.0) {
            // The state is NOT trivial: its hydrostatic stress is large.
            REQUIRE(std::abs(structural::hydrostaticStress(s).si()) > 1.0e2);
        }
    }
}

TEST_CASE("StructuralPost_ReportsVonMisesEqualToTheMagnitudeOfAUniaxialStress",
          "[structural][post]") {
    constexpr double kSigma = 250.0e6;

    SECTION("tension on each axis in turn") {
        REQUIRE_THAT(structural::vonMisesStress(stressOf(kSigma, 0, 0, 0, 0, 0)).si(),
                     WithinRel(kSigma, 1e-12));
        REQUIRE_THAT(structural::vonMisesStress(stressOf(0, kSigma, 0, 0, 0, 0)).si(),
                     WithinRel(kSigma, 1e-12));
        // szz ALONE. A plane-stress formula gives ZERO here, which makes this
        // the cheapest possible detector of one.
        REQUIRE_THAT(structural::vonMisesStress(stressOf(0, 0, kSigma, 0, 0, 0)).si(),
                     WithinRel(kSigma, 1e-12));
    }

    SECTION("compression gives the same magnitude, and it stays positive") {
        const double vm = structural::vonMisesStress(stressOf(-kSigma, 0, 0, 0, 0, 0)).si();
        REQUIRE_THAT(vm, WithinRel(kSigma, 1e-12));
        REQUIRE(vm > 0.0);
    }
}

TEST_CASE("StructuralPost_ReportsRootThreeTimesTauForEachPureShear", "[structural][post]") {
    constexpr double kTau = 80.0e6;
    const double expected = std::sqrt(3.0) * kTau;
    REQUIRE(expected != kTau);

    // vm = sqrt(3)|tau|, not |tau| and not 2|tau|. Each of the three in turn,
    // because tyz and tzx are what a 2D formula omits.
    REQUIRE_THAT(structural::vonMisesStress(stressOf(0, 0, 0, kTau, 0, 0)).si(),
                 WithinRel(expected, 1e-12));
    REQUIRE_THAT(structural::vonMisesStress(stressOf(0, 0, 0, 0, kTau, 0)).si(),
                 WithinRel(expected, 1e-12));
    REQUIRE_THAT(structural::vonMisesStress(stressOf(0, 0, 0, 0, 0, kTau)).si(),
                 WithinRel(expected, 1e-12));
    REQUIRE_THAT(structural::vonMisesStress(stressOf(0, 0, 0, 0, -kTau, 0)).si(),
                 WithinRel(expected, 1e-12));
}

TEST_CASE("StructuralPost_LeavesVonMisesUnchangedByAHydrostaticShift", "[structural][post]") {
    // ONE OF THE STRONGEST AVAILABLE TESTS: von Mises depends on the deviator
    // alone, so adding qI for ANY finite q must not move it.
    const Stress6 base = stressOf(120.0e6, -35.0e6, 55.0e6, 18.0e6, -27.0e6, 41.0e6);
    const double reference = structural::vonMisesStress(base).si();
    REQUIRE(reference > 0.0);

    for (const double q : {-1.0e9, -250.0e6, 1.0e3, 750.0e6, 2.0e9}) {
        INFO("q = " << q);
        const Stress6 moved = shifted(base, q);
        REQUIRE_THAT(structural::vonMisesStress(moved).si(), WithinRel(reference, 1e-11));
        // The hydrostatic stress DOES move, which is what makes the shift real.
        REQUIRE_THAT(structural::hydrostaticStress(moved).si(),
                     WithinRel(structural::hydrostaticStress(base).si() + q, 1e-11));
    }
}

TEST_CASE("StructuralPost_StaysFiniteAndNonNegativeAtExtremeMagnitudes",
          "[structural][post]") {
    SECTION("a very small finite stress does not produce a NaN from roundoff") {
        const Stress6 tiny = stressOf(1.0e-20, -3.0e-21, 7.0e-21, 2.0e-21, -1.0e-21, 5.0e-22);
        const double vm = structural::vonMisesStress(tiny).si();
        REQUIRE(std::isfinite(vm));
        REQUIRE(vm >= 0.0);
        REQUIRE_THAT(vm, WithinRel(deviatoricVonMises(tiny), 1e-10));
    }

    SECTION("an engineering-scale large stress stays finite") {
        const Stress6 big = stressOf(1.0e12, -4.0e11, 6.0e11, 2.0e11, -3.0e11, 5.0e11);
        const double vm = structural::vonMisesStress(big).si();
        REQUIRE(std::isfinite(vm));
        REQUIRE(vm > 0.0);
        REQUIRE_THAT(vm, WithinRel(deviatoricVonMises(big), 1e-12));
        const PrincipalStresses p = principalsOf(big);
        REQUIRE(std::isfinite(p.sigma1.si()));
        REQUIRE(std::isfinite(p.sigma3.si()));
    }
}

// ---------------------------------------------------------------------------
// Hydrostatic stress and principal strain
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_ComputesHydrostaticStressInTheNormalStressConvention",
          "[structural][post]") {
    SECTION("it is the mean of the normal components and ignores shear") {
        const Stress6 s = stressOf(30.0e6, 60.0e6, 90.0e6, 1.0e9, -2.0e9, 3.0e9);
        REQUIRE_THAT(structural::hydrostaticStress(s).si(), WithinRel(60.0e6, 1e-12));
    }

    SECTION("compression is NEGATIVE, because this is a stress and not a pressure") {
        // The sign convention, stated as a test. A `meanPressure` would be
        // +100 MPa here; `hydrostaticStress` is -100 MPa and the two must not
        // be conflated.
        const Stress6 s = stressOf(-100.0e6, -100.0e6, -100.0e6, 0, 0, 0);
        REQUIRE_THAT(structural::hydrostaticStress(s).si(), WithinRel(-100.0e6, 1e-12));
        REQUIRE(structural::hydrostaticStress(s).si() < 0.0);
    }

    SECTION("and it equals the mean of the three principal stresses") {
        const Stress6 s = stressOf(120.0e6, -35.0e6, 55.0e6, 18.0e6, -27.0e6, 41.0e6);
        const PrincipalStresses p = principalsOf(s);
        REQUIRE_THAT(structural::hydrostaticStress(s).si(),
                     WithinRel(p.sum().si() / 3.0, 1e-11));
    }
}

TEST_CASE("StructuralPost_ReportsPrincipalStrainsThroughTheHalvedShear",
          "[structural][post]") {
    constexpr double kGamma = 6.0e-4;

    SECTION("a pure engineering shear gives +gamma/2, 0, -gamma/2") {
        // THE CONVENTION TEST FOR PRINCIPAL STRAIN. If the tensor carried the
        // full engineering shear, these would be +gamma and -gamma.
        const PrincipalStrains p = principalsOf(strainOf(0, 0, 0, kGamma, 0, 0));
        REQUIRE_THAT(p.e1, WithinRel(kGamma / 2.0, 1e-10));
        REQUIRE_THAT(p.e2, WithinAbs(0.0, 1e-18));
        REQUIRE_THAT(p.e3, WithinRel(-kGamma / 2.0, 1e-10));
        REQUIRE(p.e1 != kGamma);
    }

    SECTION("and for gamma_yz and gamma_zx too") {
        for (const Strain6 e : {strainOf(0, 0, 0, 0, kGamma, 0), strainOf(0, 0, 0, 0, 0, kGamma)}) {
            const PrincipalStrains p = principalsOf(e);
            REQUIRE_THAT(p.e1, WithinRel(kGamma / 2.0, 1e-10));
            REQUIRE_THAT(p.e3, WithinRel(-kGamma / 2.0, 1e-10));
        }
    }

    SECTION("a diagonal strain comes back descending") {
        const PrincipalStrains p = principalsOf(strainOf(1.0e-4, 5.0e-4, 3.0e-4, 0, 0, 0));
        REQUIRE_THAT(p.e1, WithinRel(5.0e-4, 1e-12));
        REQUIRE_THAT(p.e2, WithinRel(3.0e-4, 1e-12));
        REQUIRE_THAT(p.e3, WithinRel(1.0e-4, 1e-12));
    }

    SECTION("the sum of principal strains is the volumetric strain") {
        const Strain6 e = strainOf(2.0e-4, -5.0e-4, 7.0e-4, 3.0e-4, -1.0e-4, 4.0e-4);
        const PrincipalStrains p = principalsOf(e);
        REQUIRE_THAT(p.sum(), WithinRel(structural::tensorOf(e).trace(), 1e-11));
    }
}

// ---------------------------------------------------------------------------
// End to end: LOAD + BC + ASSEMBLY + SOLVE + POST
// ---------------------------------------------------------------------------

namespace {

/// A block with a material, a mesh and an analysis, solvable and recoverable.
///
/// 40 x 30 x 20 mm with a 6 mm local control on one side face over a 20 mm
/// global target -- the fixture P17-ASSEMBLY-001 settled on, because the local
/// control is what gives the mesh interior nodes.
struct SolvedPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit SolvedPart(double youngs = kYoungs) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh",
            meshing::MeshControlDefinition{
                .body = feature,
                .mesh = {.sizing = {.globalTargetSize = 20_mm,
                                    .local = {meshing::LocalMeshSizing{
                                        .face = FaceName{feature,
                                                         FaceSelector{.role = FaceRole::Side,
                                                                      .entity = lines.at(0)}},
                                        .targetSize = 6_mm}}}}});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(youngs));
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(
                PoissonRatio::of(kPoisson));
        definition.mechanical.density =
            materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study =
            StructuralAnalysis::create("Study", StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        mesh();
    }

    const meshing::VolumeMesh& mesh() {
        const Result<const meshing::VolumeMesh*> generated =
            mesher.generate(document, regenerator, control);
        INFO((generated.has_value() ? std::string{} : generated.error().message));
        REQUIRE(generated.has_value());
        return **generated;
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }

    [[nodiscard]] StructuralModel model() const {
        Result<StructuralModel> prepared =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] StructuralMaterial resolved() const {
        Result<StructuralMaterial> value = structural::resolveStructuralMaterial(
            document, feature, StructuralAnalysisMode::LinearStatic);
        INFO((value.has_value() ? std::string{} : value.error().message));
        REQUIRE(value.has_value());
        return *value;
    }

    [[nodiscard]] GlobalStructuralSystem assembled(const std::vector<StructuralLoad>& loads) const {
        const StructuralModel current = model();
        const StructuralMaterial steel = resolved();
        Result<PreparedLoads> prepared = structural::prepareStructuralLoads(current, steel, loads);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        Result<GlobalStructuralSystem> system =
            structural::assembleStructuralSystem(current, steel, *prepared);
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        return std::move(*system);
    }

    /// C2: a fixed support on one whole face removes all six rigid modes.
    [[nodiscard]] ConstraintSet sufficient() const {
        const StructuralModel current = model();
        Result<MeshDofMap> numbering = structural::buildMeshDofMap(current.mesh().mesh());
        REQUIRE(numbering.has_value());
        const std::vector<StructuralRestraint> restraints{
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1), startCap())};
        Result<structural::PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(current, *numbering, restraints);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return prepared->constraints();
    }

    void setMaterialModulus(double youngs) {
        materials::MechanicalProperties mechanical =
            features::findMaterial(document, material)->definition().mechanical;
        mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(youngs));
        REQUIRE(features::setMaterialMechanical(document, material, mechanical).has_value());
    }
};

/// Every node of the end cap pushed along +Z, which a fixed start cap resists.
[[nodiscard]] std::vector<StructuralLoad> endLoad(const SolvedPart& part, double fz) {
    const StructuralModel model = part.model();
    Result<meshing::BoundaryFacetSet> facets =
        meshing::boundaryFacetsOf(model.map(), part.endCap());
    REQUIRE(facets.has_value());
    Result<std::vector<meshing::NodeId>> nodes =
        meshing::boundaryNodesOf(model.map(), model.mesh().mesh(), facets->facets);
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());
    std::vector<StructuralLoad> loads;
    std::uint64_t id = 1;
    for (const meshing::NodeId node : *nodes) {
        loads.push_back(StructuralLoad{
            LoadId::fromValue(id++),
            structural::NodalForceLoad{
                .mesh = model.mesh().mesh().stamp(),
                .node = node,
                .force = Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                 Force::fromSi(fz / static_cast<double>(nodes->size()))}}});
    }
    return loads;
}

[[nodiscard]] SolvedSystem solve(const GlobalStructuralSystem& system,
                                 const ConstraintSet& constraints) {
    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, constraints, {});
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] RecoveredFields recover(const SolvedPart& part,
                                      const GlobalStructuralSystem& system,
                                      const SolvedSystem& solution) {
    Result<RecoveredFields> fields =
        structural::recoverFields(part.model(), part.resolved(), system, solution);
    INFO((fields.has_value() ? std::string{} : fields.error().message));
    REQUIRE(fields.has_value());
    return std::move(*fields);
}

} // namespace

TEST_CASE("StructuralPost_RecoversEveryNodeAndEveryElementExactlyOnce",
          "[structural][post]") {
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());
    const RecoveredFields fields = recover(part, system, solution);

    const meshing::Mesh& mesh = part.model().mesh().mesh();
    const std::size_t nodes = mesh.nodes().size();
    const std::size_t tets = mesh.tetrahedra().size();
    INFO("nodes " << nodes << ", tets " << tets);
    REQUIRE(nodes > 0);
    REQUIRE(tets > 0);

    SECTION("the cardinalities are exact") {
        REQUIRE(fields.displacements().size() == nodes);
        REQUIRE(fields.elements().size() == tets);
        REQUIRE(fields.describes(mesh));
    }

    SECTION("every current NodeId appears exactly once, ascending") {
        REQUIRE(std::ranges::is_sorted(fields.displacements(), {}, &NodalDisplacement::node));
        for (std::size_t i = 0; i < nodes; ++i) {
            REQUIRE(fields.displacements()[i].node == mesh.nodes()[i].id);
        }
        // No duplicates: strictly ascending, which `is_sorted` alone allows to
        // repeat.
        REQUIRE(std::ranges::adjacent_find(fields.displacements(), {},
                                           &NodalDisplacement::node) ==
                fields.displacements().end());
    }

    SECTION("every current ElementId appears exactly once, ascending") {
        REQUIRE(std::ranges::is_sorted(fields.elements(), {}, &ElementFields::element));
        for (std::size_t i = 0; i < tets; ++i) {
            REQUIRE(fields.elements()[i].element == mesh.tetrahedra()[i].id);
        }
        REQUIRE(std::ranges::adjacent_find(fields.elements(), {}, &ElementFields::element) ==
                fields.elements().end());
    }

    SECTION("every value is finite, in every channel") {
        for (const NodalDisplacement& d : fields.displacements()) {
            REQUIRE(isFinite(d.displacement));
            REQUIRE(isFinite(d.magnitude));
            REQUIRE(d.magnitude.si() >= 0.0);
        }
        for (const ElementFields& e : fields.elements()) {
            REQUIRE(structural::isFinite(e.strain));
            REQUIRE(structural::isFinite(e.stress));
            REQUIRE(isFinite(e.vonMises));
            REQUIRE(isFinite(e.hydrostatic));
            REQUIRE(e.vonMises.si() >= 0.0);
            REQUIRE(isFinite(e.principalStress.sigma1));
            REQUIRE(isFinite(e.principalStress.sigma3));
            REQUIRE(std::isfinite(e.principalStrain.e1));
            REQUIRE(std::isfinite(e.principalStrain.e3));
        }
    }

    SECTION("lookup by handle agrees with the ordered channel") {
        const meshing::NodeId node = mesh.nodes().back().id;
        Result<NodalDisplacement> byHandle = fields.displacementOf(mesh, node);
        REQUIRE(byHandle.has_value());
        REQUIRE(byHandle->node == node);
        REQUIRE(byHandle->displacement == fields.displacements().back().displacement);

        const meshing::ElementId element = mesh.tetrahedra().front().id;
        Result<ElementFields> elementFields = fields.fieldsOf(mesh, element);
        REQUIRE(elementFields.has_value());
        REQUIRE(elementFields->element == element);
        REQUIRE(elementFields->stress == fields.elements().front().stress);
    }

    SECTION("a handle the mesh does not have is refused rather than guessed") {
        Result<NodalDisplacement> missing =
            fields.displacementOf(mesh, meshing::NodeId::fromValue(999999));
        REQUIRE_FALSE(missing.has_value());
        REQUIRE(missing.error().code == ErrorCode::NotFound);
        Result<ElementFields> noElement =
            fields.fieldsOf(mesh, meshing::ElementId::fromValue(999999));
        REQUIRE_FALSE(noElement.has_value());
    }

    SECTION("the summary scalars agree with the channels they summarise") {
        Length largest{};
        for (const NodalDisplacement& d : fields.displacements()) {
            if (d.magnitude > largest) {
                largest = d.magnitude;
            }
        }
        REQUIRE(fields.largestDisplacementMagnitude() == largest);
        REQUIRE(largest.si() > 0.0);

        Stress peak{};
        for (const ElementFields& e : fields.elements()) {
            if (e.vonMises > peak) {
                peak = e.vonMises;
            }
        }
        REQUIRE(fields.largestVonMises() == peak);
        REQUIRE(peak.si() > 0.0);
    }

    SECTION("the restrained nodes have exactly zero displacement and zero magnitude") {
        // The fixed start cap. EXACTLY zero, not nearly: those DOFs were never
        // unknowns, so no factorisation noise can reach them -- and a magnitude
        // of exactly zero proves the recovery read the solver's zero rather
        // than computing something near it.
        std::size_t zeroed = 0;
        for (const NodalDisplacement& d : fields.displacements()) {
            if (d.displacement.x.si() == 0.0 && d.displacement.y.si() == 0.0 &&
                d.displacement.z.si() == 0.0) {
                REQUIRE(d.magnitude.si() == 0.0);
                ++zeroed;
            }
        }
        REQUIRE(zeroed > 0);
        REQUIRE(zeroed < nodes);
    }
}

TEST_CASE("StructuralPost_GathersElementDisplacementsInTheLocalDofOrder",
          "[structural][post]") {
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());
    const RecoveredFields fields = recover(part, system, solution);
    const meshing::Mesh& mesh = part.model().mesh().mesh();

    SECTION("u_e entry i is the element's node i, in the mesh's own order") {
        // THE GATHER, TIED TO THE NODAL CHANNEL. If the gather used a
        // coordinate sort, a solver permutation or `3 * ordinal` arithmetic,
        // these would disagree.
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            Result<std::array<Translation3D, kTet4Nodes>> local =
                structural::elementDisplacements(system.numbering(), solution, tet.nodes);
            REQUIRE(local.has_value());
            for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
                Result<NodalDisplacement> nodal = fields.displacementOf(mesh, tet.nodes[corner]);
                REQUIRE(nodal.has_value());
                REQUIRE((*local)[corner] == nodal->displacement);
            }
        }
    }

    SECTION("and the twelve components sit at localDofIndex positions") {
        // Read through the numbering directly, which is the independent route:
        // the flat position of (node, component) must be what `localDofIndex`
        // says, so a reordered u_e cannot match.
        const meshing::Tetrahedron& tet = mesh.tetrahedra().front();
        Result<std::array<Translation3D, kTet4Nodes>> local =
            structural::elementDisplacements(system.numbering(), solution, tet.nodes);
        REQUIRE(local.has_value());
        std::array<double, kTet4Dofs> flat{};
        for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
            flat[structural::localDofIndex(corner, DofComponent::Ux)] =
                (*local)[corner].x.si();
            flat[structural::localDofIndex(corner, DofComponent::Uy)] =
                (*local)[corner].y.si();
            flat[structural::localDofIndex(corner, DofComponent::Uz)] =
                (*local)[corner].z.si();
        }
        for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
            Result<std::array<DofIndex, kDofsPerNode>> indices =
                system.numbering().indicesOf(tet.nodes[corner]);
            REQUIRE(indices.has_value());
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                const std::size_t position =
                    structural::localDofIndex(corner, structural::kDofComponents[offset]);
                REQUIRE(flat[position] ==
                        solution.displacementOf((*indices)[offset]).si());
            }
        }
        // INTERLEAVED, not blocked: position 3 is the SECOND node's Ux.
        REQUIRE(structural::localDofIndex(1, DofComponent::Ux) == 3);
    }

    SECTION("the element strain agrees with recovering it from the gathered u_e") {
        // Ties the stored field back to the shared B through an independent
        // gather, so a traversal that recovered the right value for the wrong
        // element would be caught.
        for (const ElementFields& stored : fields.elements()) {
            Result<ElementFields> looked = fields.fieldsOf(mesh, stored.element);
            REQUIRE(looked.has_value());
            const meshing::Tetrahedron* tet = nullptr;
            for (const meshing::Tetrahedron& candidate : mesh.tetrahedra()) {
                if (candidate.id == stored.element) {
                    tet = &candidate;
                    break;
                }
            }
            REQUIRE(tet != nullptr);
            std::array<Point3D, kTet4Nodes> corners{};
            for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
                const meshing::Node* node = mesh.findNode(tet->nodes[corner]);
                REQUIRE(node != nullptr);
                corners[corner] = node->position;
            }
            Result<std::array<Translation3D, kTet4Nodes>> local =
                structural::elementDisplacements(system.numbering(), solution, tet->nodes);
            REQUIRE(local.has_value());
            const Strain6 again = kinematicsOf(corners).strainFrom(*local);
            REQUIRE(again == stored.strain);
        }
    }
}

TEST_CASE("StructuralPost_AgreesWithItsOwnInvariantsOnASolvedModel", "[structural][post]") {
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());
    const RecoveredFields fields = recover(part, system, solution);

    // EVERY ELEMENT, against independent oracles. This is the cross-check the
    // synthetic cases cannot give: real stress states with all six components
    // populated, thousands of them.
    std::size_t withAllThreeShears = 0;
    for (const ElementFields& e : fields.elements()) {
        REQUIRE_THAT(e.vonMises.si(), WithinRel(deviatoricVonMises(e.stress), 1e-9));
        REQUIRE_THAT(e.vonMises.si(),
                     WithinRel(principalVonMises(e.principalStress.sigma1.si(),
                                                 e.principalStress.sigma2.si(),
                                                 e.principalStress.sigma3.si()),
                               1e-8));
        REQUIRE_THAT(e.principalStress.sum().si(),
                     WithinRel(structural::tensorOf(e.stress).trace().si(), 1e-8));
        REQUIRE_THAT(e.hydrostatic.si(), WithinRel(e.principalStress.sum().si() / 3.0, 1e-8));
        REQUIRE(e.principalStress.sigma1.si() >= e.principalStress.sigma2.si());
        REQUIRE(e.principalStress.sigma2.si() >= e.principalStress.sigma3.si());
        REQUIRE(e.principalStrain.e1 >= e.principalStrain.e2);
        REQUIRE(e.principalStrain.e2 >= e.principalStrain.e3);
        // The stress is D times the stored strain, through the INDEPENDENT Dref.
        const Stress6 reference = referenceStress(e.strain);
        REQUIRE_THAT(e.stress.xx.si(), WithinAbs(reference.xx.si(), 1.0));
        REQUIRE_THAT(e.stress.yz.si(), WithinAbs(reference.yz.si(), 1.0));
        REQUIRE_THAT(e.stress.zx.si(), WithinAbs(reference.zx.si(), 1.0));
        if (e.stress.zz.si() != 0.0 && e.stress.yz.si() != 0.0 && e.stress.zx.si() != 0.0) {
            ++withAllThreeShears;
        }
    }

    // THE INSTRUMENT IS NOT VACUOUS: the solved field really does produce
    // fully three-dimensional stress states, so the agreement above is not
    // being measured on a degenerate set.
    INFO("elements with all of szz, tyz, tzx nonzero: " << withAllThreeShears << " of "
                                                        << fields.elements().size());
    REQUIRE(withAllThreeShears > fields.elements().size() / 2);
    WARN("solved block | " << fields.elements().size() << " elements | "
                           << withAllThreeShears << " with all of szz, tyz, tzx nonzero | peak vm "
                           << fields.largestVonMises().si() << " Pa | peak |u| "
                           << fields.largestDisplacementMagnitude().si() << " m");
}

TEST_CASE("StructuralPost_ScalesEveryFieldWithTheLoad", "[structural][post]") {
    SolvedPart part;
    const ConstraintSet constraints = part.sufficient();
    const GlobalStructuralSystem one = part.assembled(endLoad(part, 1000.0));
    const RecoveredFields a = recover(part, one, solve(one, constraints));

    SECTION("a positive factor scales u, eps, sigma and vm all by c") {
        constexpr double kC = 2.5;
        const GlobalStructuralSystem two = part.assembled(endLoad(part, 1000.0 * kC));
        const RecoveredFields b = recover(part, two, solve(two, constraints));
        REQUIRE(a.elements().size() == b.elements().size());

        REQUIRE_THAT(b.largestDisplacementMagnitude().si(),
                     WithinRel(kC * a.largestDisplacementMagnitude().si(), 1e-9));
        REQUIRE_THAT(b.largestVonMises().si(), WithinRel(kC * a.largestVonMises().si(), 1e-9));

        for (std::size_t i = 0; i < a.elements().size(); ++i) {
            const ElementFields& ea = a.elements()[i];
            const ElementFields& eb = b.elements()[i];
            REQUIRE(ea.element == eb.element);
            if (std::abs(ea.strain.xx) > 1e-12) {
                REQUIRE_THAT(eb.strain.xx, WithinRel(kC * ea.strain.xx, 1e-9));
            }
            if (std::abs(ea.stress.zx.si()) > 1.0) {
                REQUIRE_THAT(eb.stress.zx.si(), WithinRel(kC * ea.stress.zx.si(), 1e-9));
            }
            if (ea.vonMises.si() > 1.0) {
                REQUIRE_THAT(eb.vonMises.si(), WithinRel(kC * ea.vonMises.si(), 1e-9));
            }
        }
    }

    SECTION("a negative factor flips the stress components and keeps vm positive") {
        constexpr double kC = -1.0;
        const GlobalStructuralSystem flipped = part.assembled(endLoad(part, 1000.0 * kC));
        const RecoveredFields b = recover(part, flipped, solve(flipped, constraints));

        // vm scales by |c|, so it is UNCHANGED here -- while every stress
        // component changes sign. Both halves are asserted, because either one
        // alone would pass a formula that took an absolute value too early.
        REQUIRE_THAT(b.largestVonMises().si(), WithinRel(a.largestVonMises().si(), 1e-9));
        std::size_t flippedComponents = 0;
        for (std::size_t i = 0; i < a.elements().size(); ++i) {
            const ElementFields& ea = a.elements()[i];
            const ElementFields& eb = b.elements()[i];
            if (std::abs(ea.stress.xx.si()) > 1.0) {
                REQUIRE_THAT(eb.stress.xx.si(), WithinRel(-ea.stress.xx.si(), 1e-9));
                ++flippedComponents;
            }
            REQUIRE(eb.vonMises.si() >= 0.0);
            // sigma1 and sigma3 exchange roles under a sign flip.
            if (std::abs(ea.principalStress.sigma1.si()) > 1.0) {
                REQUIRE_THAT(eb.principalStress.sigma3.si(),
                             WithinRel(-ea.principalStress.sigma1.si(), 1e-8));
            }
        }
        REQUIRE(flippedComponents > 0);
    }
}

TEST_CASE("StructuralPost_IsDeterministicAcrossRepeatedRecoveries", "[structural][post]") {
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());
    const RecoveredFields first = recover(part, system, solution);

    // FIVE REPEATS, BITWISE. `operator==` on NodalDisplacement and
    // ElementFields is defaulted over `Quantity` and `double`, so this is an
    // exact comparison of every channel including the eigenvalues -- not a
    // tolerance.
    for (int run = 0; run < 5; ++run) {
        INFO("run " << run);
        const RecoveredFields again = recover(part, system, solution);
        REQUIRE(again.displacements().size() == first.displacements().size());
        REQUIRE(again.elements().size() == first.elements().size());
        for (std::size_t i = 0; i < first.displacements().size(); ++i) {
            REQUIRE(again.displacements()[i] == first.displacements()[i]);
        }
        for (std::size_t i = 0; i < first.elements().size(); ++i) {
            REQUIRE(again.elements()[i] == first.elements()[i]);
        }
        REQUIRE(again.largestDisplacementMagnitude() == first.largestDisplacementMagnitude());
        REQUIRE(again.largestVonMises() == first.largestVonMises());
        REQUIRE(again.source() == first.source());
    }
}

TEST_CASE("StructuralPost_CarriesTheProvenanceOfWhatItRecovered", "[structural][post]") {
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());
    const RecoveredFields fields = recover(part, system, solution);

    SECTION("the source is the system's own, copied forward and not derived") {
        REQUIRE(fields.source() == system.source());
        REQUIRE(fields.source() == solution.source());
        REQUIRE(fields.source().material == part.material);
        REQUIRE(fields.source().mesh == part.model().mesh().mesh().stamp());
        REQUIRE(fields.mesh() == solution.mesh());
    }

    SECTION("and there is no accessor that could rewrite it") {
        // A COMPILE-TIME CLAIM, CHECKED AS ONE. `source()` returns a const
        // reference, so there is no non-const path to the stamp; the
        // compile-failure group holds the cases that prove assignment and
        // default construction do not compile.
        static_assert(std::is_same_v<decltype(fields.source()),
                                     const structural::AssemblySource&>);
        static_assert(!std::is_default_constructible_v<RecoveredFields>);
    }
}

// ---------------------------------------------------------------------------
// Source and currentness refusals
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_RefusesASolutionFromADifferentSystem", "[structural][post]") {
    SolvedPart part;
    const ConstraintSet constraints = part.sufficient();
    const GlobalStructuralSystem one = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solutionOfOne = solve(one, constraints);

    SECTION("a solution from a system assembled under a different modulus") {
        // SAME MESH, SAME LOADS, DIFFERENT MATERIAL REVISION. The node and
        // element counts are identical and every NodeId repeats, so only the
        // source comparison can tell them apart.
        part.setMaterialModulus(kYoungs * 2.0);
        const GlobalStructuralSystem two = part.assembled(endLoad(part, 1000.0));
        REQUIRE_FALSE(two.source() == one.source());

        Result<RecoveredFields> crossed =
            structural::recoverFields(part.model(), part.resolved(), two, solutionOfOne);
        REQUIRE_FALSE(crossed.has_value());
        REQUIRE(crossed.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(crossed.error().message, ContainsSubstring("different assembled system"));
        REQUIRE(structural::fieldRecoveryProblem(part.model(), part.resolved(), two,
                                                 solutionOfOne) ==
                RecoveryProblem::SolutionSourceMismatch);
    }
}

TEST_CASE("StructuralPost_RefusesAMaterialThatIsNotTheOneSolvedWith", "[structural][post]") {
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());

    // The displacement was solved under material A. Edit the modulus and the
    // CURRENT material is B at a new revision -- and recovering stress from
    // old u and new D would produce a number nothing downstream could detect
    // as wrong.
    part.setMaterialModulus(kYoungs * 3.0);
    const StructuralMaterial changed = part.resolved();
    REQUIRE(changed.id() == solution.source().material);
    REQUIRE(changed.revision() != solution.source().materialRevision);

    Result<RecoveredFields> refused =
        structural::recoverFields(part.model(), changed, system, solution);
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().code == ErrorCode::FailedPrecondition);
    REQUIRE_THAT(refused.error().message, ContainsSubstring("revision"));
    REQUIRE(structural::fieldRecoveryProblem(part.model(), changed, system, solution) ==
            RecoveryProblem::MaterialSourceMismatch);

    SECTION("and the identity is checked as well as the revision") {
        // A SECOND MATERIAL, so the MaterialId itself differs rather than only
        // its revision.
        features::MaterialDefinition other;
        other.designation = "Aluminium";
        other.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(70.0e9));
        other.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.33));
        other.mechanical.density =
            materials::MaterialProperty<Density>::known(Density::fromSi(2700.0));
        const Result<MaterialId> second =
            features::createMaterial(part.document, "Aluminium", other);
        REQUIRE(second.has_value());
        REQUIRE(features::assignMaterial(part.document, *second).has_value());
        requireReport(part.regenerator, part.document);

        const StructuralMaterial aluminium = part.resolved();
        REQUIRE(aluminium.id() != solution.source().material);
        REQUIRE(structural::fieldRecoveryProblem(part.model(), aluminium, system, solution) ==
                RecoveryProblem::MaterialSourceMismatch);
    }
}

TEST_CASE("StructuralPost_RefusesAResultBoundToADifferentMesh", "[structural][post]") {
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());
    const RecoveredFields fields = recover(part, system, solution);
    const meshing::MeshStamp before = part.model().mesh().mesh().stamp();

    // REMESH. The body is unchanged, so the new mesh has the same kind of
    // nodes and the ids restart from 1 -- which is exactly the adversarial
    // case: numeric NodeIds and ElementIds repeat.
    const meshing::MeshControlDefinition coarser{
        .body = part.feature, .mesh = {.sizing = {.globalTargetSize = 12_mm}}};
    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [&coarser](meshing::MeshControl& control) {
                        return control.setDefinition(coarser);
                    })
                .has_value());
    requireReport(part.regenerator, part.document);
    const meshing::VolumeMesh& remeshed = part.mesh();
    REQUIRE_FALSE(remeshed.mesh().stamp() == before);

    SECTION("the old fields no longer describe the new mesh") {
        REQUIRE_FALSE(fields.describes(remeshed.mesh()));
        // AND THE IDS REALLY DO REPEAT, so the refusal is identity-based and
        // not a count comparison.
        REQUIRE(remeshed.mesh().nodes().front().id ==
                meshing::NodeId::fromValue(1));
        REQUIRE(remeshed.mesh().tetrahedra().front().id ==
                meshing::ElementId::fromValue(1));
        // AND THE STALE LOOKUP IS REFUSED, which is the whole point of
        // `displacementOf` taking the mesh. NodeId(1) exists in BOTH meshes,
        // so a lookup that trusted the handle would return the OLD mesh's
        // displacement for a node of the new one -- a plausible number for
        // unrelated material. Identity decides.
        Result<NodalDisplacement> stale =
            fields.displacementOf(remeshed.mesh(), meshing::NodeId::fromValue(1));
        REQUIRE_FALSE(stale.has_value());
        REQUIRE(stale.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(stale.error().message, ContainsSubstring("different mesh"));
        Result<ElementFields> staleElement =
            fields.fieldsOf(remeshed.mesh(), meshing::ElementId::fromValue(1));
        REQUIRE_FALSE(staleElement.has_value());

        // Against its OWN mesh the same handle still resolves, so the refusal
        // above is about identity and not about the handle being absent.
        Result<NodalDisplacement> own =
            fields.displacementOf(part.model().mesh().mesh(), meshing::NodeId::fromValue(1));
        INFO((own.has_value() ? std::string{} : own.error().message));
        REQUIRE_FALSE(own.has_value());
    }

    SECTION("and recovery against the new mesh is refused") {
        Result<RecoveredFields> crossed =
            structural::recoverFields(part.model(), part.resolved(), system, solution);
        REQUIRE_FALSE(crossed.has_value());
        const std::optional<RecoveryProblem> problem =
            structural::fieldRecoveryProblem(part.model(), part.resolved(), system, solution);
        REQUIRE(problem.has_value());

        // WHICH CHECK CATCHES IT, AND WHY IT IS THE SECOND ONE. The system and
        // the solution are BOTH from before the remesh, so their sources agree
        // with each other and the first check cannot fire -- asserted here, so
        // the reader is not left to assume it.
        REQUIRE(solution.source() == system.source());
        // What has moved is the MODEL. The old numbering no longer describes
        // the new mesh, which is identity through the stamp and not a count.
        REQUIRE_FALSE(system.numbering().describes(part.model().mesh().mesh()));
        // Asserted EXACTLY rather than as a disjunction: the check order is
        // documented in the header, and a disjunction would let a mutation
        // that removed either check survive.
        REQUIRE(*problem == RecoveryProblem::MeshMismatch);
        REQUIRE_THAT(crossed.error().message, ContainsSubstring("different mesh"));
    }
}

TEST_CASE("StructuralPost_PropagatesAnInvalidElementThroughTheSharedKinematics",
          "[structural][post]") {
    // THE CHECK IS TESTED WHERE IT LIVES, which is the only honest place.
    // Possession of a `StructuralModel` proves P16 validated the mesh, so an
    // inverted tetrahedron cannot reach `recoverFields` today -- and this file
    // makes exactly one `computeTet4Kinematics` call, with no second
    // determinant or orientation test of its own.
    std::array<Point3D, kTet4Nodes> nodes = irregularTet();

    SECTION("an inverted tetrahedron is refused, not silently reordered") {
        std::swap(nodes[1], nodes[2]);
        Result<Tet4Kinematics> refused = structural::computeTet4Kinematics(nodes);
        REQUIRE_FALSE(refused.has_value());
        REQUIRE(structural::tet4GeometryProblem(nodes) ==
                structural::ElementProblem::InvertedElement);
    }

    SECTION("a degenerate (coplanar) tetrahedron is refused") {
        nodes[3] = Point3D{nodes[0].x + (nodes[1].x - nodes[0].x) + (nodes[2].x - nodes[0].x),
                           nodes[0].y + (nodes[1].y - nodes[0].y) + (nodes[2].y - nodes[0].y),
                           nodes[0].z + (nodes[1].z - nodes[0].z) + (nodes[2].z - nodes[0].z)};
        REQUIRE(structural::tet4GeometryProblem(nodes) ==
                structural::ElementProblem::DegenerateElement);
    }

    SECTION("and a non-finite coordinate is refused before any arithmetic") {
        nodes[2].y = Length::fromSi(std::numeric_limits<double>::quiet_NaN());
        REQUIRE(structural::tet4GeometryProblem(nodes) ==
                structural::ElementProblem::NonFiniteCoordinate);
    }

    SECTION("this file contains no second B, D or determinant") {
        // A CLAIM THE SOURCE AUDIT CHECKS, recorded here so a reader of the
        // test knows where to look: docs/verification/P17-POST-001/
        // RESULT_CONVENTIONS.md counts the occurrences.
        SUCCEED("asserted by the source audit in the evidence directory");
    }
}

TEST_CASE("StructuralPost_ReportsEveryProblemThroughTheSameOrderedChecks",
          "[structural][post]") {
    // `fieldRecoveryProblem` and `recoverFields` run one shared pass, so a
    // caller asking which problem there is cannot be told something different
    // from the caller asking for the fields.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());

    SECTION("a well-posed recovery reports no problem") {
        REQUIRE_FALSE(
            structural::fieldRecoveryProblem(part.model(), part.resolved(), system, solution)
                .has_value());
        REQUIRE(
            structural::recoverFields(part.model(), part.resolved(), system, solution).has_value());
    }

    SECTION("every value has a name, and none of them is unknown") {
        for (const RecoveryProblem problem :
             {RecoveryProblem::SolutionSourceMismatch, RecoveryProblem::MeshMismatch,
              RecoveryProblem::MaterialSourceMismatch, RecoveryProblem::MeshHasNoElements,
              RecoveryProblem::ElementNodeMissing, RecoveryProblem::DegreeOfFreedomMissing,
              RecoveryProblem::ElementRejected, RecoveryProblem::InvalidMaterial,
              RecoveryProblem::NonFiniteDisplacement, RecoveryProblem::NonFiniteStrain,
              RecoveryProblem::NonFiniteStress, RecoveryProblem::PrincipalValueFailure,
              RecoveryProblem::NonFiniteDerivedResult}) {
            REQUIRE(structural::toString(problem) != "unknown");
        }
    }

    SECTION("nothing is published on a failure") {
        part.setMaterialModulus(kYoungs * 4.0);
        const StructuralMaterial changed = part.resolved();
        REQUIRE(structural::fieldRecoveryProblem(part.model(), changed, system, solution)
                    .has_value());
        REQUIRE_FALSE(
            structural::recoverFields(part.model(), changed, system, solution).has_value());
    }
}

TEST_CASE("StructuralPost_StoresOneEntryPerNodeAndElementAndNothingQuadratic",
          "[structural][post]") {
    // THE STORAGE SHAPE, MEASURED. The result holds exactly one entry per node
    // and one per element and no element-by-node relationship, so its size is
    // O(nodes + elements) rather than O(nodes * elements).
    //
    // THE LARGE-MESH SMOKE IS IN THE REFERENCE SUITE, on RM-MESH-02. A BOX does
    // not refine under a finer surface control -- a plane is exactly
    // representable, so its surface mesh stays at two triangles per face -- and
    // measuring "a large mesh" on one would have measured nothing. That cost
    // this test an hour and is why the note is here.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const SolvedSystem solution = solve(system, part.sufficient());
    const RecoveredFields fields = recover(part, system, solution);

    const meshing::Mesh& mesh = part.model().mesh().mesh();
    const std::size_t nodes = mesh.nodes().size();
    const std::size_t tets = mesh.tetrahedra().size();
    INFO("nodes " << nodes << ", tets " << tets);

    // EXACTLY ONE ENTRY EACH, which is the cardinality half of the claim.
    REQUIRE(fields.displacements().size() == nodes);
    REQUIRE(fields.elements().size() == tets);

    // AND A FIXED COST PER ENTRY, which is the other half. Both are plain
    // aggregates of scalars with no owning member, so the per-entry cost is a
    // compile-time constant and the total is a linear function of the two
    // counts. A byte comparison against `nodes * tets` would prove nothing on
    // a mesh this small -- 19 x 58 x 8 bytes is less than the result itself --
    // so the claim is made where it is actually true.
    static_assert(std::is_trivially_copyable_v<NodalDisplacement>);
    static_assert(std::is_trivially_copyable_v<ElementFields>);
    static_assert(sizeof(NodalDisplacement) == sizeof(meshing::NodeId) + 4 * sizeof(double) ||
                  sizeof(NodalDisplacement) <= 48);
    static_assert(sizeof(ElementFields) <= 256);

    // NEITHER THE MESH NOR K IS COPIED IN. The object holds a MeshStamp, two
    // scalars and two vectors, and `sizeof` is what proves there is no mesh and
    // no matrix inside it.
    REQUIRE(sizeof(RecoveredFields) < 200);
    REQUIRE(fields.mesh() == mesh.stamp());
    // The linear GROWTH is measured in the reference suite, where a cylinder
    // can actually be refined.
}

// ---------------------------------------------------------------------------
// The per-element kernel: the analytical path and the finiteness refusals
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPost_RecoversOneElementThroughThePublicKernel", "[structural][post]") {
    // THE WHOLE PER-ELEMENT PRODUCTION PATH, on a written-down affine field.
    // `recoverFields` calls this same function once per tetrahedron, so this
    // is not a parallel implementation: it is the path, reached where its
    // inputs can be chosen.
    const Affine field{.a = {1.0e-6, 3.0e-4, -7.0e-4, 2.0e-4},
                       .b = {-2.0e-6, 5.0e-4, 1.1e-3, -4.0e-4},
                       .c = {4.0e-6, -6.0e-4, 9.0e-4, 1.3e-3}};
    const std::array<Point3D, kTet4Nodes> nodes = irregularTet();
    const ElasticityMatrix d = steelElasticity();

    Result<ElementFields> recovered = structural::recoverElementFields(
        meshing::ElementId::fromValue(7), nodes, field.sample(nodes), d);
    INFO((recovered.has_value() ? std::string{} : recovered.error().message));
    REQUIRE(recovered.has_value());

    const Strain6 expected = field.analyticStrain();
    const Stress6 expectedStress = referenceStress(expected);

    SECTION("it carries the handle it was given") {
        REQUIRE(recovered->element == meshing::ElementId::fromValue(7));
    }

    SECTION("the strain is the analytic derivative, in all six components") {
        REQUIRE_THAT(recovered->strain.xx, WithinRel(expected.xx, 1e-10));
        REQUIRE_THAT(recovered->strain.yy, WithinRel(expected.yy, 1e-10));
        REQUIRE_THAT(recovered->strain.zz, WithinRel(expected.zz, 1e-10));
        REQUIRE_THAT(recovered->strain.gammaXy, WithinRel(expected.gammaXy, 1e-10));
        REQUIRE_THAT(recovered->strain.gammaYz, WithinRel(expected.gammaYz, 1e-10));
        REQUIRE_THAT(recovered->strain.gammaZx, WithinRel(expected.gammaZx, 1e-10));
    }

    SECTION("the stress is Dref times it, through an independent constitutive law") {
        REQUIRE_THAT(recovered->stress.xx.si(), WithinRel(expectedStress.xx.si(), 1e-10));
        REQUIRE_THAT(recovered->stress.yy.si(), WithinRel(expectedStress.yy.si(), 1e-10));
        REQUIRE_THAT(recovered->stress.zz.si(), WithinRel(expectedStress.zz.si(), 1e-10));
        REQUIRE_THAT(recovered->stress.xy.si(), WithinRel(expectedStress.xy.si(), 1e-10));
        REQUIRE_THAT(recovered->stress.yz.si(), WithinRel(expectedStress.yz.si(), 1e-10));
        REQUIRE_THAT(recovered->stress.zx.si(), WithinRel(expectedStress.zx.si(), 1e-10));
    }

    SECTION("and every derived value follows, against independent oracles") {
        REQUIRE_THAT(recovered->vonMises.si(),
                     WithinRel(deviatoricVonMises(expectedStress), 1e-9));
        const std::array<double, 3> oracle =
            cardanoEigenvalues({expectedStress.xx.si(), expectedStress.yy.si(),
                                expectedStress.zz.si(), expectedStress.xy.si(),
                                expectedStress.yz.si(), expectedStress.zx.si()});
        REQUIRE_THAT(recovered->principalStress.sigma1.si(), WithinRel(oracle[0], 1e-9));
        REQUIRE_THAT(recovered->principalStress.sigma2.si(), WithinRel(oracle[1], 1e-9));
        REQUIRE_THAT(recovered->principalStress.sigma3.si(), WithinRel(oracle[2], 1e-9));
        REQUIRE_THAT(recovered->hydrostatic.si(),
                     WithinRel((oracle[0] + oracle[1] + oracle[2]) / 3.0, 1e-9));
        // The full 3D state really is full: all three of the components a 2D
        // formula drops are nonzero here.
        REQUIRE(recovered->stress.zz.si() != 0.0);
        REQUIRE(recovered->stress.yz.si() != 0.0);
        REQUIRE(recovered->stress.zx.si() != 0.0);
    }

    SECTION("and the recovery does not depend on the coordinate origin") {
        std::array<Point3D, kTet4Nodes> moved = nodes;
        for (Point3D& p : moved) {
            p.x = p.x + Length::fromSi(0.5);
            p.y = p.y + Length::fromSi(-0.25);
            p.z = p.z + Length::fromSi(1.5);
        }
        Result<ElementFields> there = structural::recoverElementFields(
            meshing::ElementId::fromValue(7), moved, field.sample(moved), d);
        REQUIRE(there.has_value());
        REQUIRE_THAT(there->vonMises.si(), WithinRel(recovered->vonMises.si(), 1e-8));
        REQUIRE_THAT(there->strain.gammaZx, WithinRel(recovered->strain.gammaZx, 1e-8));
    }
}

TEST_CASE("StructuralPost_RefusesANonFiniteOrInvalidElementInCheckOrder",
          "[structural][post]") {
    // THE FINITENESS MATRIX. Each refusal is reached through the public kernel,
    // because no validated mesh and no qualified solve can produce these --
    // which is the reason the kernel is public rather than a convenience.
    //
    // ASSERTED EXACTLY, NOT AS A DISJUNCTION. The check order is documented in
    // the header, so a NaN displacement must be reported as a non-finite
    // DISPLACEMENT and not as whatever downstream step notices it second. Three
    // P17 milestones lost a mutation kill to a disjunction added "for safety";
    // this one does not repeat it.
    const std::array<Point3D, kTet4Nodes> nodes = irregularTet();
    const ElasticityMatrix d = steelElasticity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    const auto field = [](double scale) {
        return std::array<Translation3D, kTet4Nodes>{
            Translation3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)},
            Translation3D{Length::fromSi(scale), Length::fromSi(0.0), Length::fromSi(0.0)},
            Translation3D{Length::fromSi(0.0), Length::fromSi(scale), Length::fromSi(0.0)},
            Translation3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(scale)}};
    };

    SECTION("a well-posed element succeeds, so the control is real") {
        REQUIRE_FALSE(structural::elementRecoveryProblem(nodes, field(1.0e-4), d).has_value());
        REQUIRE(structural::recoverElementFields(meshing::ElementId::fromValue(1), nodes,
                                                 field(1.0e-4), d)
                    .has_value());
    }

    SECTION("a NaN displacement is a NON-FINITE DISPLACEMENT, named as that") {
        std::array<Translation3D, kTet4Nodes> bad = field(1.0e-4);
        bad[2].y = Length::fromSi(nan);
        REQUIRE(structural::elementRecoveryProblem(nodes, bad, d) ==
                RecoveryProblem::NonFiniteDisplacement);
        Result<ElementFields> refused =
            structural::recoverElementFields(meshing::ElementId::fromValue(4), nodes, bad, d);
        REQUIRE_FALSE(refused.has_value());
        REQUIRE_THAT(refused.error().message, ContainsSubstring("element:4"));
        REQUIRE_THAT(refused.error().message, ContainsSubstring("not finite"));
    }

    SECTION("an infinite displacement, likewise") {
        std::array<Translation3D, kTet4Nodes> bad = field(1.0e-4);
        bad[0].z = Length::fromSi(-inf);
        REQUIRE(structural::elementRecoveryProblem(nodes, bad, d) ==
                RecoveryProblem::NonFiniteDisplacement);
    }

    SECTION("an inverted element is refused by the SHARED kinematics") {
        std::array<Point3D, kTet4Nodes> inverted = nodes;
        std::swap(inverted[1], inverted[2]);
        REQUIRE(structural::elementRecoveryProblem(inverted, field(1.0e-4), d) ==
                RecoveryProblem::ElementRejected);
        Result<ElementFields> refused = structural::recoverElementFields(
            meshing::ElementId::fromValue(9), inverted, field(1.0e-4), d);
        REQUIRE_FALSE(refused.has_value());
        // P17-ELEM's own words, propagated rather than restated.
        REQUIRE_THAT(refused.error().message, ContainsSubstring("element:9"));
    }

    SECTION("a degenerate element, likewise") {
        std::array<Point3D, kTet4Nodes> flat = nodes;
        flat[3] = Point3D{nodes[0].x + (nodes[1].x - nodes[0].x) + (nodes[2].x - nodes[0].x),
                          nodes[0].y + (nodes[1].y - nodes[0].y) + (nodes[2].y - nodes[0].y),
                          nodes[0].z + (nodes[1].z - nodes[0].z) + (nodes[2].z - nodes[0].z)};
        REQUIRE(structural::elementRecoveryProblem(flat, field(1.0e-4), d) ==
                RecoveryProblem::ElementRejected);
    }

    SECTION("a displacement large enough to overflow the stress is caught as a stress") {
        // A FINITE INPUT WHOSE PRODUCT IS NOT. The tetrahedron is ~2e-2 m
        // across, so a displacement of 1e300 m gives a strain near 1e302 --
        // finite -- and a stress near lambda * 1e302, which overflows a double.
        // The displacement check passes, the strain check passes, and the
        // STRESS check is the one that fires: the check order picking out the
        // first quantity that is actually not finite.
        const std::array<Translation3D, kTet4Nodes> huge = field(1.0e300);
        // The displacements themselves ARE finite, which is what makes this a
        // test of the stress check rather than of the input check.
        for (const Translation3D& u : huge) {
            REQUIRE(isFinite(u));
        }
        const std::optional<RecoveryProblem> problem =
            structural::elementRecoveryProblem(nodes, huge, d);
        REQUIRE(problem.has_value());
        REQUIRE(*problem == RecoveryProblem::NonFiniteStress);
    }

    SECTION("a displacement large enough to overflow the STRAIN is caught as a strain") {
        // THE STEP BEFORE THE STRESS CASE, and it needs a bigger number. The
        // tetrahedron is ~2.3e-2 m across, so a shape gradient is ~43 /m: a
        // displacement of 1e307 m -- still finite -- gives a strain near
        // 4e308, which overflows a double, while 1e300 gives 4e301, which does
        // not. So this reaches the STRAIN check and the 1e300 case above
        // reaches the STRESS check, which is the two of them being
        // independent rather than one shadowing the other.
        //
        // This case was added because the mutation probe that disables the
        // strain check SURVIVED without it: the branch was unreachable, not
        // untestable.
        const std::array<Translation3D, kTet4Nodes> enormous = field(1.0e307);
        for (const Translation3D& u : enormous) {
            REQUIRE(isFinite(u));
        }
        const std::optional<RecoveryProblem> problem =
            structural::elementRecoveryProblem(nodes, enormous, d);
        REQUIRE(problem.has_value());
        REQUIRE(*problem == RecoveryProblem::NonFiniteStrain);
        Result<ElementFields> refused = structural::recoverElementFields(
            meshing::ElementId::fromValue(12), nodes, enormous, d);
        REQUIRE_FALSE(refused.has_value());
        REQUIRE_THAT(refused.error().message, ContainsSubstring("strain"));
    }

    SECTION("every refusal publishes nothing at all") {
        std::array<Translation3D, kTet4Nodes> bad = field(1.0e-4);
        bad[1].x = Length::fromSi(nan);
        REQUIRE_FALSE(structural::recoverElementFields(meshing::ElementId::fromValue(1), nodes,
                                                       bad, d)
                          .has_value());
    }
}
