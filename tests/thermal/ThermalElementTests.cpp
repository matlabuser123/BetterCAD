// P18-ELEM-001: the linear Tet4 thermal element kernel.
//
// THE SPECIFICATION IS docs/verification/P18-ELEM-001/FORMULATION.md, written
// before the implementation. These tests check the code against that
// document; where a value is quoted below it was derived by hand or by the
// independent reference, never read out of production.
//
// THE INDEPENDENT REFERENCE TAKES A DIFFERENT ROUTE, which is the point of it.
// Production forms a 3x3 Jacobian, takes its determinant as a triple product
// and maps reference gradients through `J^-T`. The reference below inverts
// the 4x4 matrix `[1 x y z]` with Eigen and reads the shape-function
// coefficients straight out of it -- `Ni = ai + bi x + ci y + di z` with
// `Ni(node j) = delta_ij` -- so it never forms a Jacobian and never transposes
// one. Its `Ke` is an Eigen triple product and its `Ce` is the hand matrix
// `[2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2]` scaled, written out rather than
// integrated. It calls NO production helper.
//
// Eigen is used here and NOT in production: `src/thermal/CMakeLists.txt`
// records that no linear algebra is linked, and eigenvalues, rank and a 4x4
// inverse are exactly what a test needs and a 4x4 kernel does not.
//
// THE CAPACITY TRAP. A one-point centroid rule integrates `Ni Nj` (quadratic)
// wrongly and gives rho cp V/16 in every entry -- a rank-one matrix with the
// correct row sums. So the capacity tests pin the FULL matrix, the diagonal
// to off-diagonal ratio, the rank and the spectrum, not only conservation.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalElement.hpp>
#include <bettercad/thermal/ThermalMaterial.hpp>

#include <Eigen/Dense>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using meshing::NodeId;
using thermal::ElementProblem;
using thermal::HeatCapacity;
using thermal::InverseLength;
using thermal::kTet4CapacityPolicy;
using thermal::kTet4Nodes;
using thermal::kTet4ThermalDofs;
using thermal::Tet4Capacity;
using thermal::Tet4Conduction;
using thermal::Tet4Corners;
using thermal::Tet4Geometry;
using thermal::Tet4Temperatures;
using thermal::ThermalCapacity;
using thermal::ThermalConductance;
using thermal::ThermalMaterial;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

/// Distinctive, non-round values, so a swapped field or a stray unit factor
/// shows as a different number rather than a coincidence.
constexpr double kK = 42.75;      // W/(m K)
constexpr double kRho = 7853.25;  // kg/m^3
constexpr double kCp = 486.5;     // J/(kg K)

using Matrix34 = Eigen::Matrix<double, 3, 4>;
using Matrix4 = Eigen::Matrix4d;
using Vector4 = Eigen::Vector4d;

[[nodiscard]] Point3D at(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

[[nodiscard]] ThermalConductivity conductivity(double k) { return ThermalConductivity::fromSi(k); }

[[nodiscard]] ThermalCapacity capacity(double rho, double cp) {
    return ThermalCapacity{.density = Density::fromSi(rho),
                           .specificHeatCapacity = SpecificHeatCapacity::fromSi(cp)};
}

[[nodiscard]] Tet4Temperatures kelvin(double t1, double t2, double t3, double t4) {
    return {Temperature::fromSi(t1), Temperature::fromSi(t2), Temperature::fromSi(t3),
            Temperature::fromSi(t4)};
}

// -----------------------------------------------------------------------
// Fixtures, in metres
// -----------------------------------------------------------------------

/// The reference tetrahedron of FORMULATION.md: J = I, det J = 1, V = 1/6.
[[nodiscard]] Tet4Corners unitTet() {
    return {at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0, 0, 1)};
}

/// THE BRIEF'S GENERAL TETRAHEDRON: no edge axis-aligned, no two orthogonal,
/// so an implementation that happens to work only for the reference element
/// -- or that transposes J -- cannot pass. Its orientation is checked by the
/// independent oracle before it is used (`GeneralTetGeometry`).
[[nodiscard]] Tet4Corners generalTet() {
    return {at(0.2, -0.4, 0.1), at(1.7, 0.3, 0.2), at(0.4, 1.5, 0.6), at(0.1, 0.2, 2.0)};
}

/// A second skew fixture, deliberately not derived from the others.
[[nodiscard]] Tet4Corners skewTet() {
    return {at(0.31, -0.17, 0.44), at(1.27, 0.09, 0.21), at(0.05, 0.93, -0.38),
            at(-0.22, 0.14, 1.36)};
}

/// Valid but badly shaped: 1000:1 flat. Positive volume, so P16 calls it
/// data-valid and so must this kernel -- `invalid` is not `poor quality`.
[[nodiscard]] Tet4Corners thinTet() {
    return {at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0.3, 0.3, 1.0e-3)};
}

[[nodiscard]] std::vector<Tet4Corners> everyValidFixture() {
    return {unitTet(), generalTet(), skewTet(), thinTet()};
}

/// A positively oriented tetrahedron with pseudo-random corners, from a fixed
/// seed so the run is reproducible. Re-drawn until the volume is comfortably
/// positive, so the fixture is never a sliver by accident.
[[nodiscard]] Tet4Corners randomTet(std::mt19937& engine) {
    std::uniform_real_distribution<double> coordinate(-1.0, 1.0);
    for (;;) {
        Tet4Corners corners{};
        for (Point3D& corner : corners) {
            corner = at(coordinate(engine), coordinate(engine), coordinate(engine));
        }
        const double volume =
            meshing::signedVolume(corners[0], corners[1], corners[2], corners[3]).si();
        if (volume > 0.05) {
            return corners;
        }
    }
}

// -----------------------------------------------------------------------
// The independent reference implementation
// -----------------------------------------------------------------------

/// `V`, `G`, `Ke` and `Ce` of one Tet4 by a route that shares no code with
/// production.
struct Reference {
    double volume = 0.0;
    /// `g(axis, node)`.
    Matrix34 g = Matrix34::Zero();
    Matrix4 ke = Matrix4::Zero();
    Matrix4 ce = Matrix4::Zero();
};

/// THE DIFFERENT ROUTE. `Ni(x,y,z) = ai + bi x + ci y + di z` with
/// `Ni(node j) = delta_ij`, so with `A` the 4x4 whose rows are `[1 xj yj zj]`,
/// column `i` of `A^-1` holds node `i`'s coefficients and rows 1..3 of `A^-1`
/// hold the gradients directly. No Jacobian is formed, so a transposition in
/// production cannot be mirrored here. `V = det(A)/6`, by a 4x4 determinant
/// rather than a 3x3 triple product. `Ce` is the hand matrix, not an integral.
[[nodiscard]] Reference reference(const Tet4Corners& p, double k, double rho, double cp) {
    Matrix4 a;
    for (int i = 0; i < 4; ++i) {
        const Point3D& corner = p[static_cast<std::size_t>(i)];
        a(i, 0) = 1.0;
        a(i, 1) = corner.x.si();
        a(i, 2) = corner.y.si();
        a(i, 3) = corner.z.si();
    }
    Reference out;
    out.volume = a.determinant() / 6.0;
    const Matrix4 coefficients = a.inverse();
    for (int node = 0; node < 4; ++node) {
        for (int axis = 0; axis < 3; ++axis) {
            out.g(axis, node) = coefficients(axis + 1, node);
        }
    }
    out.ke = out.volume * k * out.g.transpose() * out.g;

    Matrix4 m;
    m << 2, 1, 1, 1,  //
        1, 2, 1, 1,   //
        1, 1, 2, 1,   //
        1, 1, 1, 2;
    out.ce = rho * cp * out.volume / 20.0 * m;
    return out;
}

// -----------------------------------------------------------------------
// Production readers, for comparison
// -----------------------------------------------------------------------

[[nodiscard]] Tet4Geometry geometryOf(const Tet4Corners& corners) {
    Result<Tet4Geometry> g = thermal::computeTet4Geometry(corners);
    INFO((g.has_value() ? std::string{} : g.error().message));
    REQUIRE(g.has_value());
    return *g;
}

[[nodiscard]] Tet4Conduction conductionOf(const Tet4Corners& corners, double k) {
    Result<Tet4Conduction> ke = thermal::computeTet4Conduction(corners, conductivity(k));
    INFO((ke.has_value() ? std::string{} : ke.error().message));
    REQUIRE(ke.has_value());
    return *ke;
}

[[nodiscard]] Tet4Capacity capacityOf(const Tet4Corners& corners, double rho, double cp) {
    Result<Tet4Capacity> ce = thermal::computeTet4Capacity(corners, capacity(rho, cp));
    INFO((ce.has_value() ? std::string{} : ce.error().message));
    REQUIRE(ce.has_value());
    return *ce;
}

[[nodiscard]] Matrix34 productionG(const Tet4Geometry& geometry) {
    Matrix34 g;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < kTet4Nodes; ++column) {
            g(static_cast<int>(row), static_cast<int>(column)) = geometry.g(row, column).si();
        }
    }
    return g;
}

[[nodiscard]] Matrix4 productionKe(const Tet4Conduction& ke) {
    Matrix4 m;
    for (std::size_t row = 0; row < kTet4ThermalDofs; ++row) {
        for (std::size_t column = 0; column < kTet4ThermalDofs; ++column) {
            m(static_cast<int>(row), static_cast<int>(column)) = ke(row, column).si();
        }
    }
    return m;
}

[[nodiscard]] Matrix4 productionCe(const Tet4Capacity& ce) {
    Matrix4 m;
    for (std::size_t row = 0; row < kTet4ThermalDofs; ++row) {
        for (std::size_t column = 0; column < kTet4ThermalDofs; ++column) {
            m(static_cast<int>(row), static_cast<int>(column)) = ce(row, column).si();
        }
    }
    return m;
}

/// `max |A - B| / max(|A|, |B|)`: a scale-aware matrix distance. Zero scale
/// falls back to the absolute distance.
template <typename A, typename B>
[[nodiscard]] double relativeDistance(const A& a, const B& b) {
    const double scale = std::max(a.cwiseAbs().maxCoeff(), b.cwiseAbs().maxCoeff());
    const double distance = (a - b).cwiseAbs().maxCoeff();
    return scale == 0.0 ? distance : distance / scale;
}

/// Eigenvalues ascending, from a self-adjoint solver, so the order is
/// deterministic.
[[nodiscard]] Vector4 eigenvaluesOf(const Matrix4& m) {
    const Eigen::SelfAdjointEigenSolver<Matrix4> solver(m);
    REQUIRE(solver.info() == Eigen::Success);
    return solver.eigenvalues();
}

/// Nodal values of `T = a + b x + c y + d z` at the corners.
[[nodiscard]] Tet4Temperatures affineField(const Tet4Corners& p, double a, double b, double c,
                                           double d) {
    Tet4Temperatures t{};
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        t[i] = Temperature::fromSi(a + b * p[i].x.si() + c * p[i].y.si() + d * p[i].z.si());
    }
    return t;
}

[[nodiscard]] Vector4 vectorOf(const Tet4Temperatures& t) {
    return Vector4(t[0].si(), t[1].si(), t[2].si(), t[3].si());
}

/// A proper rotation that is not about any single axis:
/// `R = Rz(31 deg) Ry(-17 deg) Rx(23 deg)`. Checked orthogonal with
/// determinant +1 by the test that uses it, before it is used.
[[nodiscard]] Eigen::Matrix3d properRotation() {
    const double deg = std::numbers::pi / 180.0;
    const Eigen::Matrix3d r = (Eigen::AngleAxisd(31.0 * deg, Eigen::Vector3d::UnitZ()) *
                               Eigen::AngleAxisd(-17.0 * deg, Eigen::Vector3d::UnitY()) *
                               Eigen::AngleAxisd(23.0 * deg, Eigen::Vector3d::UnitX()))
                                  .toRotationMatrix();
    return r;
}

[[nodiscard]] Tet4Corners transformed(const Tet4Corners& p, const Eigen::Matrix3d& r,
                                      const Eigen::Vector3d& t) {
    Tet4Corners out{};
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        const Eigen::Vector3d x(p[i].x.si(), p[i].y.si(), p[i].z.si());
        const Eigen::Vector3d y = r * x + t;
        out[i] = at(y(0), y(1), y(2));
    }
    return out;
}

[[nodiscard]] Tet4Corners scaled(const Tet4Corners& p, double s) {
    Tet4Corners out{};
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        out[i] = at(s * p[i].x.si(), s * p[i].y.si(), s * p[i].z.si());
    }
    return out;
}

/// Thermal properties with the given k and cp, either or both absent.
[[nodiscard]] materials::ThermalProperties thermalWith(std::optional<double> k,
                                                       std::optional<double> cp) {
    materials::ThermalProperties properties;
    if (k) {
        properties.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(*k));
    }
    if (cp) {
        properties.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(*cp));
    }
    return properties;
}

[[nodiscard]] materials::MechanicalProperties mechanicalWith(std::optional<double> rho) {
    materials::MechanicalProperties properties;
    if (rho) {
        properties.density = materials::MaterialProperty<Density>::known(Density::fromSi(*rho));
    }
    return properties;
}

/// A block whose material the test sets, for the P18-MAT-001 integration
/// cases: the element is handed what `resolveThermalMaterial` resolved, not
/// a hand-built value.
struct MaterialPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};

    MaterialPart() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    void assign(const std::string& name, std::optional<double> k, std::optional<double> rho,
                std::optional<double> cp) {
        features::MaterialDefinition definition;
        definition.designation = name;
        definition.mechanical = mechanicalWith(rho);
        definition.thermal = thermalWith(k, cp);
        const Result<MaterialId> id = features::createMaterial(document, name, definition);
        INFO((id.has_value() ? std::string{} : id.error().message));
        REQUIRE(id.has_value());
        REQUIRE(features::assignMaterial(document, *id).has_value());
        requireReport(regenerator, document);
    }

    [[nodiscard]] ThermalMaterial resolve(thermal::ThermalAnalysisMode mode) {
        Result<ThermalMaterial> material = thermal::resolveThermalMaterial(document, feature, mode);
        INFO((material.has_value() ? std::string{} : material.error().message));
        REQUIRE(material.has_value());
        return *material;
    }
};

} // namespace

// ===========================================================================
// Geometry
// ===========================================================================

TEST_CASE("ThermalElement_UnitTetGeometry", "[thermal][element][geometry]") {
    // The primary exact oracle: J = I, det J = 1, V = 1/6, and G is the
    // reference gradient table itself. Every expected value here is written
    // by hand from FORMULATION.md.
    const Tet4Geometry g = geometryOf(unitTet());
    CHECK(g.determinant().si() == 1.0);
    CHECK_THAT(g.volume().si(), WithinRel(1.0 / 6.0, 1e-15));

    const std::array<std::array<double, 3>, 4> expected{
        {{-1, -1, -1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    double maxError = 0.0;
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        const std::array<InverseLength, 3> gradient = g.gradient(node);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            maxError = std::max(maxError, std::abs(gradient[axis].si() - expected[node][axis]));
            // And G(axis, node) IS gradient(node)[axis]: row is the axis,
            // column is the corner.
            CHECK(g.g(axis, node).si() == gradient[axis].si());
        }
    }
    INFO("max |G - G_expected| = " << maxError);
    CHECK(maxError <= 1e-15);
    WARN(std::format("unit tet: det J {} V {} max |G - hand| {}", g.determinant().si(), g.volume().si(),
                     maxError));

    SECTION("G is 3 x 4 and nothing more") {
        CHECK(g.g(3, 0).si() == 0.0);
        CHECK(g.g(0, 4).si() == 0.0);
        CHECK(g.gradient(4) == std::array<InverseLength, 3>{});
    }
}

TEST_CASE("ThermalElement_GeneralTetGeometry", "[thermal][element][geometry]") {
    // The unit tetrahedron alone cannot see a transposed Jacobian (J = I).
    // The brief's general fixture can, and its expected G comes from the
    // independent [1 x y z] inverse, never from production.
    const Tet4Corners p = generalTet();
    const Reference ref = reference(p, kK, kRho, kCp);
    REQUIRE(ref.volume > 0.0);  // positively oriented, checked before use

    const Tet4Geometry g = geometryOf(p);
    const double volumeError = std::abs(g.volume().si() - ref.volume) / ref.volume;
    const double gError = relativeDistance(productionG(g), ref.g);
    INFO("V " << g.volume().si() << " vs " << ref.volume << " rel " << volumeError);
    INFO("G rel " << gError);
    CHECK(volumeError <= 1e-14);
    CHECK(gError <= 1e-13);
    WARN(std::format("general tet: V {} (oracle {}) rel {:.2e}; max rel |G - oracle| {:.2e}",
                     g.volume().si(), ref.volume, volumeError, gError));

    // And the same for the other fixtures and a batch of random ones: the
    // oracle route and the Jacobian route agree everywhere, not only once.
    std::mt19937 engine(18'001);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 25; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& corners : fixtures) {
        const Reference r = reference(corners, kK, kRho, kCp);
        const Tet4Geometry q = geometryOf(corners);
        worst = std::max(worst, relativeDistance(productionG(q), r.g));
        CHECK_THAT(q.volume().si(), WithinRel(r.volume, 1e-12));
    }
    INFO("worst G distance over " << fixtures.size() << " fixtures " << worst);
    CHECK(worst <= 1e-12);
}

TEST_CASE("ThermalElement_VolumeAgreesWithMeshingSignedVolumeBitwise",
          "[thermal][element][geometry]") {
    // V = det J / 6 is the SAME expression meshing::signedVolume computes,
    // grouped the same way, so the two are equal bit for bit -- not within a
    // tolerance. The two are predicates on the same boundary.
    std::mt19937 engine(18'002);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 50; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    for (const Tet4Corners& p : fixtures) {
        const Tet4Geometry g = geometryOf(p);
        const double expected = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
        CHECK(g.volume().si() == expected);
        CHECK_THAT(g.determinant().si(), WithinRel(6.0 * g.volume().si(), 1e-15));
    }
}

TEST_CASE("ThermalElement_ShapeGradientPartitionOfUnity", "[thermal][element][geometry]") {
    // sum_i grad Ni = 0, i.e. G [1 1 1 1]^T = 0, measured rather than true by
    // construction: production computes all four gradients by the full
    // product.
    std::mt19937 engine(18'003);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 25; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Tet4Geometry g = geometryOf(p);
        const Matrix34 m = productionG(g);
        const Eigen::Vector3d sum = m * Vector4::Ones();
        const double scale = m.cwiseAbs().maxCoeff();
        worst = std::max(worst, sum.cwiseAbs().maxCoeff() / scale);
    }
    INFO("worst |G 1| / |G| " << worst);
    CHECK(worst <= 1e-14);
    WARN(std::format("partition of unity: worst |sum grad Ni| / |G| = {:.2e} over {} fixtures", worst,
                     fixtures.size()));
}

TEST_CASE("ThermalElement_ShapeGradientCoordinateReproduction", "[thermal][element][geometry]") {
    // T_i = x_i must give grad T = e_x, and likewise for y and z. This is the
    // test that catches a swapped or transposed gradient component.
    std::mt19937 engine(18'004);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 10; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    std::array<double, 3> worst{};
    for (const Tet4Corners& p : fixtures) {
        const Tet4Geometry g = geometryOf(p);
        const std::array<thermal::TemperatureGradient3, 3> grads{
            g.gradientFrom(affineField(p, 0, 1, 0, 0)), g.gradientFrom(affineField(p, 0, 0, 1, 0)),
            g.gradientFrom(affineField(p, 0, 0, 0, 1))};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const std::array<double, 3> got{grads[axis].x.si(), grads[axis].y.si(),
                                            grads[axis].z.si()};
            for (std::size_t c = 0; c < 3; ++c) {
                const double expected = c == axis ? 1.0 : 0.0;
                worst[axis] = std::max(worst[axis], std::abs(got[c] - expected));
            }
        }
    }
    INFO("x " << worst[0] << " y " << worst[1] << " z " << worst[2]);
    CHECK(worst[0] <= 1e-13);
    CHECK(worst[1] <= 1e-13);
    CHECK(worst[2] <= 1e-13);
    WARN(std::format("coordinate reproduction: worst error x {:.2e} y {:.2e} z {:.2e}", worst[0], worst[1],
                     worst[2]));
}

TEST_CASE("ThermalElement_AffineTemperatureGradient", "[thermal][element][geometry]") {
    // THE CORE PATCH-ELEMENT ORACLE: T = 305.7 + 7.2 x - 3.5 y + 11.4 z must
    // give grad T = (7.2, -3.5, 11.4) K/m on any valid tetrahedron, and the
    // constant must disappear.
    std::mt19937 engine(18'005);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 25; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    // The error of G Te is bounded by the size of the products it sums, not
    // by the size of the answer: the 305.7 K offset multiplies entries of G
    // that cancel, so a sliver (|G| ~ 1e3 /m) loses digits in proportion to
    // |G||T|. The assertion is in those units -- error / (eps |G| |T|) --
    // with a bound of 64, and the absolute error is recorded beside it.
    const double eps = std::numeric_limits<double>::epsilon();
    double worst = 0.0;
    double worstUnits = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Tet4Geometry g = geometryOf(p);
        const Tet4Temperatures t = affineField(p, 305.7, 7.2, -3.5, 11.4);
        const thermal::TemperatureGradient3 grad = g.gradientFrom(t);
        const double error = std::max({std::abs(grad.x.si() - 7.2), std::abs(grad.y.si() + 3.5),
                                       std::abs(grad.z.si() - 11.4)});
        const double scale = productionG(g).cwiseAbs().maxCoeff() * vectorOf(t).cwiseAbs().maxCoeff();
        worst = std::max(worst, error);
        worstUnits = std::max(worstUnits, error / (eps * scale));
    }
    INFO("worst affine gradient error (K/m) " << worst << ", in eps |G||T| units " << worstUnits);
    CHECK(worstUnits <= 64.0);
    WARN(std::format("affine field: worst |grad T - (7.2, -3.5, 11.4)| = {:.2e} K/m = {:.3g} eps |G||T| (bound 64)",
                     worst, worstUnits));

    SECTION("the gradient is a TemperatureGradient3 in K/m, by type") {
        STATIC_CHECK(std::is_same_v<decltype(geometryOf(unitTet()).gradientFrom(Tet4Temperatures{})),
                                    thermal::TemperatureGradient3>);
        STATIC_CHECK(TemperatureGradient::dimension == dimensions::temperature / dimensions::length);
    }
}

TEST_CASE("ThermalElement_ConstantFieldHasZeroGradient", "[thermal][element][geometry]") {
    // T0 = 327.4 K everywhere: grad T = 0 and Ke T = 0, independent of T0
    // and not only for zero.
    for (const Tet4Corners& p : everyValidFixture()) {
        const Tet4Geometry g = geometryOf(p);
        const Tet4Temperatures uniform = kelvin(327.4, 327.4, 327.4, 327.4);
        const thermal::TemperatureGradient3 grad = g.gradientFrom(uniform);
        const double scale = productionG(g).cwiseAbs().maxCoeff() * 327.4;
        CHECK(std::abs(grad.x.si()) <= 1e-13 * scale);
        CHECK(std::abs(grad.y.si()) <= 1e-13 * scale);
        CHECK(std::abs(grad.z.si()) <= 1e-13 * scale);
        const Tet4Conduction ke = conductionOf(p, kK);
        const double flowScale = productionKe(ke).cwiseAbs().maxCoeff() * 327.4;
        for (const Power q : ke.heatFlowFrom(uniform)) {
            CHECK(std::abs(q.si()) <= 1e-13 * flowScale);
        }
    }
}

// ===========================================================================
// Orientation and degeneracy
// ===========================================================================

TEST_CASE("ThermalElement_InvertedTetRejected", "[thermal][element][orientation]") {
    // An odd swap flips det J. The element is REFUSED -- not reordered, not
    // computed on |det J| -- and the chain overloads refuse too, so no matrix
    // can be obtained from an inverted tetrahedron by any public path.
    Tet4Corners p = generalTet();
    std::swap(p[1], p[2]);
    REQUIRE(meshing::signedVolume(p[0], p[1], p[2], p[3]).si() < 0.0);

    CHECK(thermal::tet4GeometryProblem(p) == std::optional<ElementProblem>{ElementProblem::InvertedElement});
    const Result<Tet4Geometry> g = thermal::computeTet4Geometry(p);
    REQUIRE_FALSE(g.has_value());
    CHECK(g.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(g.error().message, ContainsSubstring("inverted"));
    CHECK_THAT(g.error().message, ContainsSubstring("not reordered"));
    CHECK_FALSE(thermal::computeTet4Conduction(p, conductivity(kK)).has_value());
    CHECK_FALSE(thermal::computeTet4Capacity(p, capacity(kRho, kCp)).has_value());

    // Swapping back restores validity: it was the ORDER, not the points.
    std::swap(p[1], p[2]);
    CHECK_FALSE(thermal::tet4GeometryProblem(p).has_value());
}

TEST_CASE("ThermalElement_CoplanarTetRejected", "[thermal][element][orientation]") {
    const Tet4Corners flat{at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(1, 1, 0)};
    REQUIRE(meshing::signedVolume(flat[0], flat[1], flat[2], flat[3]).si() == 0.0);
    CHECK(thermal::tet4GeometryProblem(flat) == std::optional<ElementProblem>{ElementProblem::DegenerateElement});
    const Result<Tet4Geometry> g = thermal::computeTet4Geometry(flat);
    REQUIRE_FALSE(g.has_value());
    CHECK(g.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(g.error().message, ContainsSubstring("degenerate"));
    CHECK_FALSE(thermal::computeTet4Conduction(flat, conductivity(kK)).has_value());
    CHECK_FALSE(thermal::computeTet4Capacity(flat, capacity(kRho, kCp)).has_value());

    SECTION("three collinear corners and a fourth off the line") {
        const Tet4Corners collinear{at(0, 0, 0), at(1, 0, 0), at(2, 0, 0), at(0, 1, 1)};
        CHECK(thermal::tet4GeometryProblem(collinear) ==
              std::optional<ElementProblem>{ElementProblem::DegenerateElement});
    }
}

TEST_CASE("ThermalElement_CoincidentNodeTetRejected", "[thermal][element][orientation]") {
    for (std::size_t a = 0; a < kTet4Nodes; ++a) {
        for (std::size_t b = a + 1; b < kTet4Nodes; ++b) {
            Tet4Corners p = generalTet();
            p[b] = p[a];
            INFO("corners " << a << " and " << b << " coincide");
            CHECK(thermal::tet4GeometryProblem(p) ==
                  std::optional<ElementProblem>{ElementProblem::DegenerateElement});
            CHECK_FALSE(thermal::computeTet4Geometry(p).has_value());
        }
    }
}

TEST_CASE("ThermalElement_NearDegeneratePolicy", "[thermal][element][orientation]") {
    // THE POLICY HAS NO TOLERANCE, like P16's and P17's. On one side of the
    // boundary -- det J > 0, however small -- the element is computed and its
    // result checked for finiteness; on the other -- det J == 0 exactly -- it
    // is refused. A 1000:1 sliver is data-valid; `invalid` is not `poor
    // quality`, and quality is P18-VALID-001's.
    SECTION("a thin but positively oriented tetrahedron is ACCEPTED") {
        const Tet4Corners p = thinTet();
        REQUIRE(meshing::signedVolume(p[0], p[1], p[2], p[3]).si() > 0.0);
        CHECK_FALSE(thermal::tet4GeometryProblem(p).has_value());
        const Tet4Geometry g = geometryOf(p);
        CHECK(g.volume().si() > 0.0);
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            for (const InverseLength component : g.gradient(node)) {
                CHECK(std::isfinite(component.si()));
            }
        }
        // And its matrices are finite and still satisfy the invariants.
        const Matrix4 ke = productionKe(conductionOf(p, kK));
        CHECK(ke.allFinite());
        CHECK((ke * Vector4::Ones()).cwiseAbs().maxCoeff() <= 1e-12 * ke.cwiseAbs().maxCoeff());
        WARN(std::format("sliver 1000:1: V {:.3e} m^3, max |Ke| {:.3e} W/K, computed and finite",
                         g.volume().si(), ke.cwiseAbs().maxCoeff()));
    }

    SECTION("both sides of the rounding-noise bound, at three scales") {
        // THE BOUND IS RELATIVE: B = 16 eps |e1||e2||e3|, homogeneous of
        // degree three, so the verdict is the same at every scale. A base of
        // side s with height h above it has det J = s^2 h and
        // |e1||e2||e3| ~ s^2 sqrt(0.18 s^2 + h^2); the ratio det / product is
        // ~ h / (0.42 s). Heights at 1e-12 and 1e-17 of the scale straddle
        // B / product = 3.55e-15 -- measured, printed below.
        const double b = 16.0 * std::numeric_limits<double>::epsilon();
        for (const double s : {1e-3, 1.0, 1e2}) {
            INFO("scale " << s);
            const double hAbove = 1e-12 * s;   // ratio ~ 2.4e-12 > 3.55e-15
            const double hBelow = 1e-17 * s;   // ratio ~ 2.4e-17 < 3.55e-15
            const Tet4Corners above{at(0, 0, 0), at(s, 0, 0), at(0, s, 0), at(0.3 * s, 0.3 * s, hAbove)};
            const Tet4Corners below{at(0, 0, 0), at(s, 0, 0), at(0, s, 0), at(0.3 * s, 0.3 * s, hBelow)};
            const Tet4Corners on{at(0, 0, 0), at(s, 0, 0), at(0, s, 0), at(0.3 * s, 0.3 * s, 0.0)};
            CHECK_FALSE(thermal::tet4GeometryProblem(above).has_value());
            CHECK(thermal::tet4GeometryProblem(below) ==
                  std::optional<ElementProblem>{ElementProblem::DegenerateElement});
            CHECK(thermal::tet4GeometryProblem(on) ==
                  std::optional<ElementProblem>{ElementProblem::DegenerateElement});
            // Above the bound the classification is P16's own: the same sign
            // from the same expression.
            CHECK(meshing::signedVolume(above[0], above[1], above[2], above[3]).si() > 0.0);
            CHECK_THAT(geometryOf(above).determinant().si(), WithinRel(s * s * hAbove, 1e-12));
        }
        WARN(std::format("rounding-noise bound B = 16 eps |e1||e2||e3| = {:.3e} * product of edge lengths; the "
                         "test straddles it at 1e-12 and 1e-17 of the scale, at three scales",
                         b));
    }

    SECTION("the verdict does not depend on the scale") {
        // x' = s x multiplies det J and B by s^3, so every tetrahedron is
        // classified the same at every scale. Checked on fixtures of each
        // kind: valid, inverted, and coplanar by construction.
        Tet4Corners inverted = generalTet();
        std::swap(inverted[0], inverted[1]);
        const Tet4Corners coplanar{at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0.3, 0.7, 0.0)};
        for (const double s : {1e-3, 0.1, 1.0, 1e2}) {
            INFO("scale " << s);
            CHECK_FALSE(thermal::tet4GeometryProblem(scaled(generalTet(), s)).has_value());
            CHECK(thermal::tet4GeometryProblem(scaled(inverted, s)) ==
                  std::optional<ElementProblem>{ElementProblem::InvertedElement});
            CHECK(thermal::tet4GeometryProblem(scaled(coplanar, s)) ==
                  std::optional<ElementProblem>{ElementProblem::DegenerateElement});
        }
    }
}

TEST_CASE("ThermalElement_RoundingNoiseIsBelowTheDegeneracyBound", "[thermal][element][orientation]") {
    // THE MEASUREMENT BEHIND THE BOUND. Build tetrahedra that are coplanar or
    // have coincident corners IN EXACT ARITHMETIC but not in floating point,
    // from coordinates that are not integers, and compare the noise the
    // determinant actually produces with B. The kernel must call every one of
    // them degenerate; the margin shows 16 is not tuned to a lucky fixture;
    // and the count of cases P16's exact-zero rule would have called valid or
    // inverted records WHY the refinement exists.
    std::mt19937 engine(18'020);
    std::uniform_real_distribution<double> coordinate(-1.0, 1.0);
    std::uniform_real_distribution<double> weight(0.1, 0.9);
    const double eps = std::numeric_limits<double>::epsilon();
    double worstRatio = 0.0;
    int exactZeroWouldAccept = 0;
    int cases = 0;
    const auto edge = [](const Point3D& a, const Point3D& b) {
        const double dx = b.x.si() - a.x.si();
        const double dy = b.y.si() - a.y.si();
        const double dz = b.z.si() - a.z.si();
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const auto measure = [&](const Tet4Corners& p) {
        ++cases;
        const double v = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
        if (v != 0.0) {
            ++exactZeroWouldAccept;
        }
        const double product = edge(p[0], p[1]) * edge(p[0], p[2]) * edge(p[0], p[3]);
        if (product > 0.0) {
            worstRatio = std::max(worstRatio, std::abs(6.0 * v) / (eps * product));
        }
        CHECK(thermal::tet4GeometryProblem(p) == std::optional<ElementProblem>{ElementProblem::DegenerateElement});
        CHECK_FALSE(thermal::computeTet4Geometry(p).has_value());
        CHECK_FALSE(thermal::computeTet4Conduction(p, conductivity(kK)).has_value());
    };
    for (int i = 0; i < 400; ++i) {
        const Point3D a = at(coordinate(engine), coordinate(engine), coordinate(engine));
        const Point3D b = at(coordinate(engine), coordinate(engine), coordinate(engine));
        const Point3D c = at(coordinate(engine), coordinate(engine), coordinate(engine));
        // COPLANAR: the fourth corner is an affine combination of the other
        // three, computed in floating point.
        const double w1 = weight(engine);
        const double w2 = weight(engine);
        const Point3D d = at(a.x.si() + w1 * (b.x.si() - a.x.si()) + w2 * (c.x.si() - a.x.si()),
                             a.y.si() + w1 * (b.y.si() - a.y.si()) + w2 * (c.y.si() - a.y.si()),
                             a.z.si() + w1 * (b.z.si() - a.z.si()) + w2 * (c.z.si() - a.z.si()));
        measure({a, b, c, d});
        // COINCIDENT: every pair of corners the same point.
        measure({a, b, b, c});
        measure({a, b, c, b});
        measure({a, b, c, c});
        measure({a, a, b, c});
    }
    INFO("worst |det J| / (eps |e1||e2||e3|) over " << cases << " degenerate fixtures: " << worstRatio);
    // The bound is 16; the noise measured must sit below it with margin.
    CHECK(worstRatio < 16.0);
    WARN(std::format("rounding noise: worst |det J| / (eps |e1||e2||e3|) = {:.3g} over {} exactly-degenerate "
                     "fixtures (bound 16); the exact-zero rule would have accepted {} of them",
                     worstRatio, cases, exactZeroWouldAccept));
}

TEST_CASE("ThermalElement_NonFiniteCoordinateRejected", "[thermal][element][orientation]") {
    for (const double bad : {kNaN, kInf, -kInf}) {
        for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
            Tet4Corners p = unitTet();
            p[corner] = at(0.0, bad, 0.0);
            INFO("coordinate " << bad << " at corner " << corner);
            CHECK(thermal::tet4GeometryProblem(p) ==
                  std::optional<ElementProblem>{ElementProblem::NonFiniteCoordinate});
            const Result<Tet4Geometry> g = thermal::computeTet4Geometry(p);
            REQUIRE_FALSE(g.has_value());
            CHECK(g.error().code == ErrorCode::InvalidArgument);
        }
    }
}

TEST_CASE("ThermalElement_NonFiniteResultRejected", "[thermal][element][orientation]") {
    SECTION("a determinant so small that /6 underflows is degenerate, as P16 classifies it") {
        // det J = h exactly for this shape; the smallest subnormal over six
        // is zero, and P16's own volume test calls a zero volume degenerate.
        // The third edge is (0, 0, h): its length is h, so the rounding-noise
        // bound 16 eps |e1||e2||e3| underflows to zero and det J = h > B.
        const double h = std::numeric_limits<double>::denorm_min();
        const Tet4Corners p{at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0, 0, h)};
        REQUIRE(meshing::signedVolume(p[0], p[1], p[2], p[3]).si() == 0.0);
        CHECK(thermal::tet4GeometryProblem(p) == std::optional<ElementProblem>{ElementProblem::DegenerateElement});
    }

    SECTION("a positive subnormal determinant whose reciprocal overflows") {
        // det J = h exactly for this shape, and h = 1e-310 is subnormal: the
        // volume h/6 is representable and positive, the orientation check
        // passes, and the gradient of the fourth corner is 1/h = inf. The
        // RESULT check refuses it: a finiteness gate on the output, not a
        // tolerance on the input.
        // A needle whose third edge is itself subnormal, so the bound
        // (proportional to that edge) underflows and does not mask it.
        const double h = 1e-310;
        const Tet4Corners p{at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0, 0, h)};
        REQUIRE(meshing::signedVolume(p[0], p[1], p[2], p[3]).si() > 0.0);
        CHECK_FALSE(thermal::tet4GeometryProblem(p).has_value());
        const Result<Tet4Geometry> g = thermal::computeTet4Geometry(p);
        REQUIRE_FALSE(g.has_value());
        CHECK(g.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(g.error().message, ContainsSubstring("not finite"));
        CHECK_FALSE(thermal::computeTet4Conduction(p, conductivity(kK)).has_value());
    }

    SECTION("coordinates whose triple product overflows are degenerate, as P16 classifies them") {
        const Tet4Corners huge{at(0, 0, 0), at(1e200, 0, 0), at(0, 1e200, 0), at(0, 0, 1e200)};
        CHECK(thermal::tet4GeometryProblem(huge) ==
              std::optional<ElementProblem>{ElementProblem::DegenerateElement});
    }

    SECTION("a conduction entry that overflows is refused, not emitted") {
        // Valid geometry (a unit tetrahedron scaled to 1e5 m: V = 1.7e14 m^3,
        // |grad N|^2 = 3e-10 /m^2) and a valid, finite, positive k = 1e305.
        // The product k V alone is 1e305 * 1.7e14 = 1.7e319, which does not fit
        // in a double, so every entry is infinite. The finiteness gate on Ke
        // refuses it, and no infinite matrix is returned.
        const Tet4Geometry g = geometryOf(scaled(unitTet(), 1e5));
        const Result<Tet4Conduction> ke = thermal::computeTet4Conduction(g, conductivity(1e305));
        REQUIRE_FALSE(ke.has_value());
        CHECK(ke.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(ke.error().message, ContainsSubstring("not finite"));
        // And the same element with a conductivity that fits is fine.
        CHECK(thermal::computeTet4Conduction(g, conductivity(1e290)).has_value());
    }
}

// ===========================================================================
// Conduction
// ===========================================================================

TEST_CASE("ThermalElement_UnitTetConductionAnalytical", "[thermal][element][conduction]") {
    // THE HAND MATRIX. k = 6 W/(m K) on the unit tetrahedron makes k/6 = 1,
    // so Ke = G^T G exactly:
    //     [ 3 -1 -1 -1 ]
    //     [-1  1  0  0 ]
    //     [-1  0  1  0 ]
    //     [-1  0  0  1 ]   W/K
    // written out here, not generated.
    Matrix4 expected;
    expected << 3, -1, -1, -1,  //
        -1, 1, 0, 0,            //
        -1, 0, 1, 0,            //
        -1, 0, 0, 1;
    const Matrix4 actual = productionKe(conductionOf(unitTet(), 6.0));
    const double absError = (actual - expected).cwiseAbs().maxCoeff();
    INFO("Ke =\n" << actual);
    CHECK(absError <= 1e-15);
    WARN(std::format("unit tet Ke (k = 6): max |Ke - hand| = {:.2e} W/K, max rel {:.2e}", absError,
                     absError / 3.0));

    // The analytically zero off-diagonals are zero or within an ulp of it,
    // and were NOT zeroed by a threshold: the kernel has none.
    CHECK(std::abs(actual(1, 2)) <= 1e-16);
    CHECK(std::abs(actual(2, 3)) <= 1e-16);

    SECTION("the entries are in W/K, by type") {
        STATIC_CHECK(std::is_same_v<decltype(conductionOf(unitTet(), 6.0)(0, 0)), ThermalConductance>);
        STATIC_CHECK(ThermalConductance::dimension == dimensions::power / dimensions::temperature);
    }
}

TEST_CASE("ThermalElement_GeneralTetConductionOracle", "[thermal][element][conduction]") {
    // Against the [1 x y z] oracle, with a realistic conductivity, on the
    // brief's general tetrahedron and on random ones.
    std::mt19937 engine(18'006);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 25; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Reference ref = reference(p, kK, kRho, kCp);
        const Matrix4 ke = productionKe(conductionOf(p, kK));
        worst = std::max(worst, relativeDistance(ke, ref.ke));
    }
    const Reference general = reference(generalTet(), kK, kRho, kCp);
    const double generalError = relativeDistance(productionKe(conductionOf(generalTet(), kK)), general.ke);
    INFO("general " << generalError << " worst " << worst);
    CHECK(generalError <= 1e-13);
    CHECK(worst <= 1e-12);
    WARN(std::format("general tet Ke vs oracle: rel {:.2e}; worst over {} fixtures {:.2e}", generalError,
                     fixtures.size(), worst));
}

TEST_CASE("ThermalElement_ConductionSymmetric", "[thermal][element][conduction]") {
    // Symmetric BY CONSTRUCTION of grad Ni . grad Nj, measured as
    // max |Kij - Kji|. Not symmetrised afterwards: the probe that adds a
    // 0.5 (K + K^T) is a mutation, not a fix.
    std::mt19937 engine(18'007);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 50; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worstAbsolute = 0.0;
    double worstRelative = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Matrix4 ke = productionKe(conductionOf(p, kK));
        const double asym = (ke - ke.transpose()).cwiseAbs().maxCoeff();
        worstAbsolute = std::max(worstAbsolute, asym);
        worstRelative = std::max(worstRelative, asym / ke.cwiseAbs().maxCoeff());
    }
    INFO("worst |Kij - Kji| " << worstAbsolute << " relative " << worstRelative);
    // The dot product is commutative term by term, so the two entries are
    // the same floating-point computation: exactly equal, and that is what is
    // asserted, with the measured value recorded either way.
    CHECK(worstAbsolute == 0.0);
    WARN(std::format("conduction symmetry: max |Kij - Kji| = {:.2e} (relative {:.2e}) over {} fixtures",
                     worstAbsolute, worstRelative, fixtures.size()));
}

TEST_CASE("ThermalElement_ConductionConstantTemperatureNull", "[thermal][element][conduction]") {
    // Ke [1 1 1 1]^T = 0 and 1^T Ke 1 = 0: a uniform temperature conducts
    // nothing. Then the consequence: T + c 1 has the same heat flow and the
    // same quadratic form as T, for any T and any c.
    std::mt19937 engine(18'008);
    std::uniform_real_distribution<double> value(250.0, 450.0);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worstNull = 0.0;
    double worstShift = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Tet4Conduction ke = conductionOf(p, kK);
        const Matrix4 m = productionKe(ke);
        const double scale = m.cwiseAbs().maxCoeff();
        const Vector4 null = m * Vector4::Ones();
        worstNull = std::max(worstNull, null.cwiseAbs().maxCoeff() / scale);
        CHECK(std::abs(Vector4::Ones().dot(null)) <= 1e-13 * scale);

        const Tet4Temperatures t = kelvin(value(engine), value(engine), value(engine), value(engine));
        const double c = 123.456;
        const Tet4Temperatures shifted = kelvin(t[0].si() + c, t[1].si() + c, t[2].si() + c, t[3].si() + c);
        const std::array<Power, 4> f0 = ke.heatFlowFrom(t);
        const std::array<Power, 4> f1 = ke.heatFlowFrom(shifted);
        for (std::size_t i = 0; i < kTet4Nodes; ++i) {
            worstShift = std::max(worstShift, std::abs(f0[i].si() - f1[i].si()) / (scale * 600.0));
        }
        const double q0 = vectorOf(t).dot(m * vectorOf(t));
        const double q1 = vectorOf(shifted).dot(m * vectorOf(shifted));
        CHECK_THAT(q1, WithinRel(q0, 1e-9));
    }
    INFO("worst |Ke 1| / |Ke| " << worstNull << " worst flow shift " << worstShift);
    CHECK(worstNull <= 1e-14);
    CHECK(worstShift <= 1e-13);
    WARN(std::format("constant null mode: worst |Ke 1| / |Ke| = {:.2e}; add-constant flow change {:.2e}",
                     worstNull, worstShift));
}

TEST_CASE("ThermalElement_ConductionRowSumsZero", "[thermal][element][conduction]") {
    std::mt19937 engine(18'009);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Matrix4 m = productionKe(conductionOf(p, kK));
        const double scale = m.cwiseAbs().maxCoeff();
        for (int row = 0; row < 4; ++row) {
            worst = std::max(worst, std::abs(m.row(row).sum()) / scale);
        }
        // Sum of all entries, likewise.
        CHECK(std::abs(m.sum()) <= 1e-13 * scale);
        // And every diagonal entry is positive: k V |grad Ni|^2 > 0.
        for (int i = 0; i < 4; ++i) {
            CHECK(m(i, i) > 0.0);
        }
    }
    INFO("worst row sum / |Ke| " << worst);
    CHECK(worst <= 1e-14);
    WARN(std::format("row sums: worst |sum_j Kij| / |Ke| = {:.2e}", worst));
}

TEST_CASE("ThermalElement_ConductionPositiveSemidefinite", "[thermal][element][conduction]") {
    // x^T Ke x >= 0 for random x, and the spectrum: one eigenvalue at zero,
    // three positive. NOT positive definite -- a free element has the uniform
    // temperature as a genuine null mode and nothing regularises it away.
    std::mt19937 engine(18'010);
    std::uniform_real_distribution<double> value(-1.0, 1.0);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double minQuadratic = std::numeric_limits<double>::infinity();
    double worstNullEigen = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Matrix4 m = productionKe(conductionOf(p, kK));
        const double scale = m.cwiseAbs().maxCoeff();
        for (int trial = 0; trial < 50; ++trial) {
            const Vector4 x(value(engine), value(engine), value(engine), value(engine));
            const double q = x.dot(m * x) / (scale * x.squaredNorm());
            minQuadratic = std::min(minQuadratic, q);
            CHECK(q >= -1e-14);
        }
        const Vector4 lambda = eigenvaluesOf(m);
        worstNullEigen = std::max(worstNullEigen, std::abs(lambda(0)) / lambda(3));
        CHECK(std::abs(lambda(0)) <= 1e-13 * lambda(3));
        // The 1000:1 sliver has a condition number of ~1e6 (measured),
        // so the three positive eigenvalues are separated from the null one
        // by far less than for a well-shaped element; 1e-9 of the largest is
        // still twelve orders above the null eigenvalue.
        CHECK(lambda(1) > 1e-9 * lambda(3));
        CHECK(lambda(2) > 1e-9 * lambda(3));
        CHECK(lambda(3) > 0.0);
    }
    const Vector4 unit = eigenvaluesOf(productionKe(conductionOf(unitTet(), 6.0)));
    INFO("unit tet eigenvalues " << unit.transpose());
    // By hand: G^T G for the unit tet has eigenvalues 0, 1, 1, 4.
    CHECK(std::abs(unit(0)) <= 1e-14);
    CHECK_THAT(unit(1), WithinRel(1.0, 1e-13));
    CHECK_THAT(unit(2), WithinRel(1.0, 1e-13));
    CHECK_THAT(unit(3), WithinRel(4.0, 1e-13));
    WARN(std::format("PSD: min x^T Ke x / (|Ke| |x|^2) = {:.2e}; worst |lambda_0| / lambda_3 = {:.2e}; unit tet "
                     "eigenvalues {:.3g} {:.3g} {:.3g} {:.3g}",
                     minQuadratic, worstNullEigen, unit(0), unit(1), unit(2), unit(3)));
}

TEST_CASE("ThermalElement_ConductionRankThree", "[thermal][element][conduction]") {
    // Rank 3, nullity 1, and the null vector IS the constant mode.
    std::mt19937 engine(18'011);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    for (const Tet4Corners& p : fixtures) {
        const Matrix4 m = productionKe(conductionOf(p, kK));
        const Vector4 lambda = eigenvaluesOf(m);
        int rank = 0;
        for (int i = 0; i < 4; ++i) {
            if (lambda(i) > 1e-10 * lambda(3)) {
                ++rank;
            }
        }
        CHECK(rank == 3);
        const Eigen::SelfAdjointEigenSolver<Matrix4> solver(m);
        const Vector4 nullVector = solver.eigenvectors().col(0);
        // Parallel to [1 1 1 1]: every component equal in magnitude.
        const double mean = nullVector.mean();
        CHECK((nullVector - mean * Vector4::Ones()).cwiseAbs().maxCoeff() <= 1e-7);
    }
}

TEST_CASE("ThermalElement_ConductionEnergyIdentity", "[thermal][element][conduction]") {
    // Te^T Ke Te = k V |G Te|^2, with the right-hand side computed
    // INDEPENDENTLY: for an affine field T = a + b x + c y + d z the
    // gradient is (b, c, d) by hand, so the identity is k V (b^2 + c^2 + d^2)
    // and `a` must disappear.
    std::mt19937 engine(18'012);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    // The quadratic form sums products of size |T|^T |Ke| |T|, and the
    // constant offset makes those products cancel to the (much smaller)
    // energy, so the achievable error is eps |T|^T |Ke| |T| and not
    // eps * energy. A sliver, with |Ke| ~ 1e6 W/K, shows it plainly. The
    // assertions are in those units, bound 64, and the relative error is
    // printed beside them.
    const double eps = std::numeric_limits<double>::epsilon();
    double worst = 0.0;
    double worstUnits = 0.0;
    double worstNetUnits = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Reference ref = reference(p, kK, kRho, kCp);
        const Tet4Conduction ke = conductionOf(p, kK);
        const Matrix4 m = productionKe(ke);
        const double rhs = kK * ref.volume * (7.2 * 7.2 + 3.5 * 3.5 + 11.4 * 11.4);
        for (const double offset : {305.7, -40.0}) {
            const Tet4Temperatures temperatures = affineField(p, offset, 7.2, -3.5, 11.4);
            const Vector4 t = vectorOf(temperatures);
            const double lhs = t.dot(m * t);
            const double products = t.cwiseAbs().dot(m.cwiseAbs() * t.cwiseAbs());
            worst = std::max(worst, std::abs(lhs - rhs) / rhs);
            worstUnits = std::max(worstUnits, std::abs(lhs - rhs) / (eps * products));
            // And the nodal flows sum to zero: no net source inside an element.
            double net = 0.0;
            for (const Power q : ke.heatFlowFrom(temperatures)) {
                net += q.si();
            }
            const double flowProducts = Vector4::Ones().dot(m.cwiseAbs() * t.cwiseAbs());
            worstNetUnits = std::max(worstNetUnits, std::abs(net) / (eps * flowProducts));
        }
    }
    INFO("worst energy identity error " << worst << ", in eps |T||Ke||T| units " << worstUnits);
    CHECK(worstUnits <= 64.0);
    CHECK(worstNetUnits <= 64.0);
    WARN(std::format("energy identity: worst relative error {:.2e} = {:.3g} eps |T|^T|Ke||T| (bound 64); net nodal "
                     "flow {:.3g} eps 1^T|Ke||T| (bound 64)",
                     worst, worstUnits, worstNetUnits));
}

TEST_CASE("ThermalElement_ConductivityScaling", "[thermal][element][conduction][scaling]") {
    // Ke(c k) = c Ke(k) for c = 0.25, 2, 10, and over k = 0.2, 15, 400.
    const Tet4Corners p = generalTet();
    const Matrix4 base = productionKe(conductionOf(p, kK));
    for (const double c : {0.25, 2.0, 10.0}) {
        const Matrix4 scaledKe = productionKe(conductionOf(p, c * kK));
        const double error = relativeDistance(scaledKe, c * base);
        INFO("c " << c << " error " << error);
        CHECK(error <= 1e-15);
    }
    const Matrix4 unitKe = productionKe(conductionOf(p, 1.0));
    for (const double k : {0.2, 15.0, 400.0}) {
        const Matrix4 ke = productionKe(conductionOf(p, k));
        CHECK(relativeDistance(ke, k * unitKe) <= 1e-15);
    }
    WARN("conductivity scaling: Ke(c k) = c Ke(k) to 1e-15 for c in {0.25, 2, 10} and k in {0.2, 15, 400}");
}

TEST_CASE("ThermalElement_ConductionRejectsInvalidConductivity", "[thermal][element][conduction]") {
    // k must be finite and positive. Not abs'd: -42.75 is refused, not
    // accepted as 42.75.
    const Tet4Geometry g = geometryOf(generalTet());
    for (const double bad : {0.0, -42.75, kNaN, kInf, -kInf}) {
        INFO("k " << bad);
        CHECK(thermal::conductivityProblem(conductivity(bad)) ==
              std::optional<ElementProblem>{ElementProblem::InvalidConductivity});
        const Result<Tet4Conduction> ke = thermal::computeTet4Conduction(g, conductivity(bad));
        REQUIRE_FALSE(ke.has_value());
        CHECK(ke.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(ke.error().message, ContainsSubstring("conductivity"));
    }
    CHECK_FALSE(thermal::conductivityProblem(conductivity(kK)).has_value());
    // The negative value was not silently made positive.
    CHECK_FALSE(thermal::computeTet4Conduction(generalTet(), conductivity(-kK)).has_value());
}

TEST_CASE("ThermalElement_ConductionNeedsNoCapacity", "[thermal][element][conduction]") {
    // Ke from geometry and k only. The signature takes no density, no
    // specific heat and no material object -- so the steady path can never
    // be blocked by a missing transient property. Stated as a type check and
    // exercised.
    STATIC_CHECK(std::is_invocable_v<decltype(static_cast<Result<Tet4Conduction> (*)(const Tet4Geometry&, ThermalConductivity)>(
                                                  &thermal::computeTet4Conduction)),
                                     const Tet4Geometry&, ThermalConductivity>);
    const Tet4Geometry g = geometryOf(generalTet());
    const Result<Tet4Conduction> ke = thermal::computeTet4Conduction(g, conductivity(kK));
    REQUIRE(ke.has_value());
    CHECK((*ke)(0, 0).si() > 0.0);
}

// ===========================================================================
// Capacity
// ===========================================================================

TEST_CASE("ThermalElement_UnitTetCapacityAnalytical", "[thermal][element][capacity]") {
    // THE HAND MATRIX. rho cp = 120 J/(m^3 K) on the unit tetrahedron
    // (V = 1/6) makes rho cp V/20 = 1, so
    //     Ce = [2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2]  J/K
    // exactly. rho = 12, cp = 10 are TEST-ONLY scaling values, not a
    // material; the realistic fixture is a separate case.
    Matrix4 expected;
    expected << 2, 1, 1, 1,  //
        1, 2, 1, 1,          //
        1, 1, 2, 1,          //
        1, 1, 1, 2;
    const Matrix4 actual = productionCe(capacityOf(unitTet(), 12.0, 10.0));
    const double absError = (actual - expected).cwiseAbs().maxCoeff();
    INFO("Ce =\n" << actual);
    CHECK(absError <= 1e-15);
    double diagonalError = 0.0;
    double offDiagonalError = 0.0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const double e = std::abs(actual(i, j) - expected(i, j));
            (i == j ? diagonalError : offDiagonalError) = std::max(i == j ? diagonalError : offDiagonalError, e);
        }
    }
    WARN(std::format("unit tet Ce (rho cp = 120): max |Ce - hand| = {:.2e} J/K; diagonal {:.2e}, off-diagonal {:.2e}",
                     absError, diagonalError, offDiagonalError));

    SECTION("the entries are in J/K, by type, and total() is rho cp V") {
        STATIC_CHECK(std::is_same_v<decltype(capacityOf(unitTet(), 12.0, 10.0)(0, 0)), HeatCapacity>);
        STATIC_CHECK(HeatCapacity::dimension == dimensions::energy / dimensions::temperature);
        CHECK_THAT(capacityOf(unitTet(), 12.0, 10.0).total().si(), WithinRel(120.0 / 6.0, 1e-14));
    }
}

TEST_CASE("ThermalElement_GeneralTetCapacityOracle", "[thermal][element][capacity]") {
    std::mt19937 engine(18'013);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 25; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Reference ref = reference(p, kK, kRho, kCp);
        worst = std::max(worst, relativeDistance(productionCe(capacityOf(p, kRho, kCp)), ref.ce));
    }
    const Reference general = reference(generalTet(), kK, kRho, kCp);
    const double generalError = relativeDistance(productionCe(capacityOf(generalTet(), kRho, kCp)), general.ce);
    CHECK(generalError <= 1e-14);
    CHECK(worst <= 1e-13);
    WARN(std::format("general tet Ce vs oracle: rel {:.2e}; worst over {} fixtures {:.2e}", generalError,
                     fixtures.size(), worst));
}

TEST_CASE("ThermalElement_CapacityConsistentPolicy", "[thermal][element][capacity]") {
    // THE FULL CONSISTENT MATRIX, entry by entry: diag = rho cp V/10,
    // off-diag = rho cp V/20, Ce_ii = 2 Ce_ij, every off-diagonal positive,
    // trace 2 rho cp V/5. Row sums alone would not distinguish this from the
    // centroid matrix; these do.
    CHECK(kTet4CapacityPolicy == "consistent");
    std::mt19937 engine(18'014);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worstDiagonal = 0.0;
    double worstOff = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const double v = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
        const double rhoCpV = kRho * kCp * v;
        const Matrix4 ce = productionCe(capacityOf(p, kRho, kCp));
        for (int i = 0; i < 4; ++i) {
            worstDiagonal = std::max(worstDiagonal, std::abs(ce(i, i) - rhoCpV / 10.0) / (rhoCpV / 10.0));
            for (int j = 0; j < 4; ++j) {
                if (i == j) {
                    continue;
                }
                worstOff = std::max(worstOff, std::abs(ce(i, j) - rhoCpV / 20.0) / (rhoCpV / 20.0));
                CHECK(ce(i, j) > 0.0);
                CHECK_THAT(ce(i, i), WithinRel(2.0 * ce(i, j), 1e-15));
            }
        }
        CHECK_THAT(ce.trace(), WithinRel(2.0 * rhoCpV / 5.0, 1e-14));
    }
    CHECK(worstDiagonal <= 1e-14);
    CHECK(worstOff <= 1e-14);
    WARN(std::format("consistent capacity: worst rel error diagonal {:.2e} (rho cp V/10), off-diagonal {:.2e} "
                     "(rho cp V/20); policy '{}'",
                     worstDiagonal, worstOff, kTet4CapacityPolicy));
}

TEST_CASE("ThermalElement_CapacitySymmetric", "[thermal][element][capacity]") {
    std::mt19937 engine(18'015);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 30; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const Matrix4 ce = productionCe(capacityOf(p, kRho, kCp));
        worst = std::max(worst, (ce - ce.transpose()).cwiseAbs().maxCoeff());
    }
    CHECK(worst == 0.0);
    WARN(std::format("capacity symmetry: max |Cij - Cji| = {:.2e} over {} fixtures", worst, fixtures.size()));
}

TEST_CASE("ThermalElement_CapacityPositiveDefinite", "[thermal][element][capacity]") {
    // Every eigenvalue positive, x^T Ce x > 0 for every non-zero x, and
    // Ce 1 != 0: a uniform temperature RATE stores heat. The one way Ce and
    // Ke differ in kind.
    std::mt19937 engine(18'016);
    std::uniform_real_distribution<double> value(-1.0, 1.0);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double minRatio = std::numeric_limits<double>::infinity();
    for (const Tet4Corners& p : fixtures) {
        const Matrix4 ce = productionCe(capacityOf(p, kRho, kCp));
        const Vector4 lambda = eigenvaluesOf(ce);
        CHECK(lambda(0) > 0.0);
        minRatio = std::min(minRatio, lambda(0) / lambda(3));
        for (int trial = 0; trial < 30; ++trial) {
            const Vector4 x(value(engine), value(engine), value(engine), value(engine));
            if (x.squaredNorm() == 0.0) {
                continue;
            }
            CHECK(x.dot(ce * x) > 0.0);
        }
        const double v = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
        CHECK((ce * Vector4::Ones()).cwiseAbs().minCoeff() > 0.0);
        CHECK_THAT((ce * Vector4::Ones()).sum(), WithinRel(kRho * kCp * v, 1e-13));
    }
    // lambda_min / lambda_max = (rho cp V/20) / (rho cp V/4) = 1/5, exactly
    // the spectrum of [2 1 1 1; ...] scaled.
    CHECK_THAT(minRatio, WithinRel(0.2, 1e-12));
    WARN(std::format("capacity SPD: min eigenvalue ratio lambda_min / lambda_max = {:.15g} (expected 0.2)", minRatio));
}

TEST_CASE("ThermalElement_CapacityFullRank", "[thermal][element][capacity]") {
    // Rank 4, and the spectrum is rho cp V/4 once and rho cp V/20 three
    // times: eigenvalues 5, 1, 1, 1 of the integer matrix, scaled.
    std::mt19937 engine(18'017);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    for (const Tet4Corners& p : fixtures) {
        const double v = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
        const double rhoCpV = kRho * kCp * v;
        const Matrix4 ce = productionCe(capacityOf(p, kRho, kCp));
        const Vector4 lambda = eigenvaluesOf(ce);
        int rank = 0;
        for (int i = 0; i < 4; ++i) {
            if (lambda(i) > 1e-10 * lambda(3)) {
                ++rank;
            }
        }
        CHECK(rank == 4);
        CHECK_THAT(lambda(0), WithinRel(rhoCpV / 20.0, 1e-12));
        CHECK_THAT(lambda(1), WithinRel(rhoCpV / 20.0, 1e-12));
        CHECK_THAT(lambda(2), WithinRel(rhoCpV / 20.0, 1e-12));
        CHECK_THAT(lambda(3), WithinRel(rhoCpV / 4.0, 1e-12));
    }
    const Vector4 unit = eigenvaluesOf(productionCe(capacityOf(unitTet(), 12.0, 10.0)));
    WARN(std::format("capacity spectrum, unit tet rho cp = 120: {:.15g} {:.15g} {:.15g} {:.15g} (expected 1 1 1 5)",
                     unit(0), unit(1), unit(2), unit(3)));
}

TEST_CASE("ThermalElement_CapacityRowSums", "[thermal][element][capacity]") {
    // Each row sums to rho cp V/4: a uniform rate gives each node a quarter
    // of the capacity.
    std::mt19937 engine(18'018);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const double v = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
        const Matrix4 ce = productionCe(capacityOf(p, kRho, kCp));
        for (int row = 0; row < 4; ++row) {
            worst = std::max(worst, std::abs(ce.row(row).sum() - kRho * kCp * v / 4.0) / (kRho * kCp * v / 4.0));
        }
    }
    CHECK(worst <= 1e-14);
    WARN(std::format("capacity row sums: worst rel error vs rho cp V/4 = {:.2e}", worst));
}

TEST_CASE("ThermalElement_CapacityTotalHeatCapacity", "[thermal][element][capacity]") {
    // ones^T Ce ones = rho cp V, in J/K; total() reports the same; and a
    // uniform rate alpha stores rho cp V alpha, in W.
    std::mt19937 engine(18'019);
    std::vector<Tet4Corners> fixtures = everyValidFixture();
    for (int i = 0; i < 20; ++i) {
        fixtures.push_back(randomTet(engine));
    }
    double worst = 0.0;
    for (const Tet4Corners& p : fixtures) {
        const double v = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
        const Tet4Capacity ce = capacityOf(p, kRho, kCp);
        const Matrix4 m = productionCe(ce);
        const double total = Vector4::Ones().dot(m * Vector4::Ones());
        worst = std::max(worst, std::abs(total - kRho * kCp * v) / (kRho * kCp * v));
        CHECK_THAT(ce.total().si(), WithinRel(kRho * kCp * v, 1e-13));
        CHECK_THAT(m.sum(), WithinRel(kRho * kCp * v, 1e-13));
        const double alpha = 2.5;  // K/s
        CHECK_THAT((m * (alpha * Vector4::Ones())).sum(), WithinRel(kRho * kCp * v * alpha, 1e-13));
    }
    CHECK(worst <= 1e-14);
    WARN(std::format("total heat capacity: worst |1^T Ce 1 - rho cp V| / rho cp V = {:.2e}", worst));
}

TEST_CASE("ThermalElement_OnePointQuadratureIsNotTheConsistentMatrix", "[thermal][element][capacity]") {
    // THE TRAP, made explicit. The centroid rule with Ni = 1/4 gives
    // rho cp V/16 in every entry: the same row sums, the same total, rank
    // ONE. Production must differ from it in every entry, and the tests
    // above that pin the diagonal, the ratio and the spectrum are what fail
    // if someone later integrates with one point -- or lumps.
    const Tet4Corners p = generalTet();
    const double v = meshing::signedVolume(p[0], p[1], p[2], p[3]).si();
    const double rhoCpV = kRho * kCp * v;
    const Matrix4 centroid = rhoCpV / 16.0 * Matrix4::Ones();
    const Matrix4 lumped = rhoCpV / 4.0 * Matrix4::Identity();
    const Matrix4 ce = productionCe(capacityOf(p, kRho, kCp));

    // Same conservation ...
    CHECK_THAT(centroid.sum(), WithinRel(ce.sum(), 1e-13));
    CHECK_THAT(lumped.sum(), WithinRel(ce.sum(), 1e-13));
    for (int row = 0; row < 4; ++row) {
        CHECK_THAT(centroid.row(row).sum(), WithinRel(ce.row(row).sum(), 1e-13));
    }
    // ... different matrices, in every entry.
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            CHECK(std::abs(ce(i, j) - centroid(i, j)) > 0.1 * rhoCpV / 16.0);
            CHECK(std::abs(ce(i, j) - lumped(i, j)) > 0.1 * rhoCpV / 20.0);
        }
    }
    // Rank: the centroid matrix is rank one; production is rank four.
    const Vector4 centroidEigen = eigenvaluesOf(centroid);
    CHECK(std::abs(centroidEigen(2)) <= 1e-12 * centroidEigen(3));
    const Vector4 productionEigen = eigenvaluesOf(ce);
    CHECK(productionEigen(0) > 1e-3 * productionEigen(3));
    WARN(std::format("one-point trap: centroid rank 1 (eigenvalues {:.3g} {:.3g} {:.3g} {:.3g}), production rank 4 "
                     "(eigenvalues {:.3g} {:.3g} {:.3g} {:.3g}); same row sums rho cp V/4 = {:.6g}",
                     centroidEigen(0), centroidEigen(1), centroidEigen(2), centroidEigen(3), productionEigen(0),
                     productionEigen(1), productionEigen(2), productionEigen(3), rhoCpV / 4.0));
}

TEST_CASE("ThermalElement_DensityScaling", "[thermal][element][capacity][scaling]") {
    const Tet4Corners p = generalTet();
    const Matrix4 base = productionCe(capacityOf(p, kRho, kCp));
    for (const double c : {0.25, 2.0, 10.0}) {
        CHECK(relativeDistance(productionCe(capacityOf(p, c * kRho, kCp)), c * base) <= 1e-15);
    }
    // Representative pairs: Ce depends on the product only.
    const Matrix4 a = productionCe(capacityOf(p, 1200.0, 800.0));
    const Matrix4 b = productionCe(capacityOf(p, 7800.0, 500.0));
    CHECK(relativeDistance(a, (1200.0 * 800.0) / (7800.0 * 500.0) * b) <= 1e-14);
    WARN("density scaling: Ce(c rho) = c Ce(rho) to 1e-15 for c in {0.25, 2, 10}");
}

TEST_CASE("ThermalElement_SpecificHeatScaling", "[thermal][element][capacity][scaling]") {
    const Tet4Corners p = generalTet();
    const Matrix4 base = productionCe(capacityOf(p, kRho, kCp));
    for (const double c : {0.25, 2.0, 10.0}) {
        CHECK(relativeDistance(productionCe(capacityOf(p, kRho, c * kCp)), c * base) <= 1e-15);
    }
    WARN("specific-heat scaling: Ce(c cp) = c Ce(cp) to 1e-15 for c in {0.25, 2, 10}");
}

TEST_CASE("ThermalElement_CapacityRejectsInvalidDensity", "[thermal][element][capacity]") {
    const Tet4Geometry g = geometryOf(generalTet());
    for (const double bad : {0.0, -kRho, kNaN, kInf, -kInf}) {
        INFO("rho " << bad);
        CHECK(thermal::capacityProblem(capacity(bad, kCp)) ==
              std::optional<ElementProblem>{ElementProblem::InvalidDensity});
        const Result<Tet4Capacity> ce = thermal::computeTet4Capacity(g, capacity(bad, kCp));
        REQUIRE_FALSE(ce.has_value());
        CHECK(ce.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(ce.error().message, ContainsSubstring("density"));
    }
    // Not abs'd.
    CHECK_FALSE(thermal::computeTet4Capacity(generalTet(), capacity(-kRho, kCp)).has_value());
}

TEST_CASE("ThermalElement_CapacityRejectsInvalidSpecificHeat", "[thermal][element][capacity]") {
    const Tet4Geometry g = geometryOf(generalTet());
    for (const double bad : {0.0, -kCp, kNaN, kInf, -kInf}) {
        INFO("cp " << bad);
        CHECK(thermal::capacityProblem(capacity(kRho, bad)) ==
              std::optional<ElementProblem>{ElementProblem::InvalidSpecificHeat});
        const Result<Tet4Capacity> ce = thermal::computeTet4Capacity(g, capacity(kRho, bad));
        REQUIRE_FALSE(ce.has_value());
        CHECK(ce.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(ce.error().message, ContainsSubstring("specific heat"));
    }
    // Both bad: the density is named first, so the diagnostic names the
    // first thing to fix and the two are not confused.
    CHECK(thermal::capacityProblem(capacity(-1.0, -1.0)) ==
          std::optional<ElementProblem>{ElementProblem::InvalidDensity});
    CHECK_FALSE(thermal::capacityProblem(capacity(kRho, kCp)).has_value());
}

TEST_CASE("ThermalElement_CapacityNeedsNoConductivity", "[thermal][element][capacity]") {
    // Ce from geometry and {rho, cp} only: no k in the signature, so a
    // conductivity cannot enter it, and Ce is unchanged whatever k an
    // analysis also carries.
    const Tet4Geometry g = geometryOf(generalTet());
    const Result<Tet4Capacity> ce = thermal::computeTet4Capacity(g, capacity(kRho, kCp));
    REQUIRE(ce.has_value());
    const double v = g.volume().si();
    // The total is rho cp V and NOT k V, k rho V, k cp V or anything else k
    // could have slipped into.
    CHECK_THAT(ce->total().si(), WithinRel(kRho * kCp * v, 1e-13));
    CHECK(std::abs(ce->total().si() - kK * v) > 0.5 * kRho * kCp * v);
    CHECK(std::abs(ce->total().si() - kK * kRho * v) > 0.5 * kRho * kCp * v);
}

TEST_CASE("ThermalElement_CapacityNonFiniteProduct", "[thermal][element][capacity]") {
    // rho and cp each finite and positive, but rho cp V overflows: refused
    // as NonFiniteResult, never emitted as an infinite matrix.
    const Tet4Geometry g = geometryOf(unitTet());
    const Result<Tet4Capacity> ce = thermal::computeTet4Capacity(g, capacity(1e200, 1e200));
    REQUIRE_FALSE(ce.has_value());
    CHECK(ce.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(ce.error().message, ContainsSubstring("not finite"));
    // The diagnostic names the PRODUCT that overflowed, with its three
    // factors, rather than only the first matrix entry that came out
    // infinite -- the root cause, not the first symptom. (A survivor of the
    // first mutation batch: the per-entry gate alone also refuses this, with
    // a message about an entry.)
    CHECK_THAT(ce.error().message, ContainsSubstring("rho cp V"));
    CHECK_THAT(ce.error().message, ContainsSubstring("kg/m^3"));
    CHECK_FALSE(thermal::capacityProblem(capacity(1e200, 1e200)).has_value());
}

TEST_CASE("ThermalElement_CapacityPhysicalMaterial", "[thermal][element][capacity]") {
    // One realistic-ish material, so nothing relies on neat hand-scaled
    // values: steel-like k = 45, rho = 7800, cp = 500.
    const Tet4Corners p = generalTet();
    const Reference ref = reference(p, 45.0, 7800.0, 500.0);
    CHECK(relativeDistance(productionCe(capacityOf(p, 7800.0, 500.0)), ref.ce) <= 1e-14);
    CHECK(relativeDistance(productionKe(conductionOf(p, 45.0)), ref.ke) <= 1e-13);
    CHECK_THAT(capacityOf(p, 7800.0, 500.0).total().si(), WithinRel(7800.0 * 500.0 * ref.volume, 1e-13));
}

// ===========================================================================
// Transformations
// ===========================================================================

TEST_CASE("ThermalElement_TranslationInvariant", "[thermal][element][transform]") {
    // t = (12.3, -7.1, 4.9) m: det J, V, G, Ke and Ce unchanged within
    // rounding of the edge subtraction.
    const Tet4Corners base = generalTet();
    const Tet4Corners moved = transformed(base, Eigen::Matrix3d::Identity(), Eigen::Vector3d(12.3, -7.1, 4.9));
    const Tet4Geometry g0 = geometryOf(base);
    const Tet4Geometry g1 = geometryOf(moved);
    const double vError = std::abs(g1.volume().si() - g0.volume().si()) / g0.volume().si();
    const double gError = relativeDistance(productionG(g1), productionG(g0));
    const double keError = relativeDistance(productionKe(conductionOf(moved, kK)), productionKe(conductionOf(base, kK)));
    const double ceError = relativeDistance(productionCe(capacityOf(moved, kRho, kCp)), productionCe(capacityOf(base, kRho, kCp)));
    INFO("V " << vError << " G " << gError << " Ke " << keError << " Ce " << ceError);
    CHECK(vError <= 1e-13);
    CHECK(gError <= 1e-13);
    CHECK(keError <= 1e-13);
    CHECK(ceError <= 1e-13);
    CHECK_THAT(g1.determinant().si(), WithinRel(g0.determinant().si(), 1e-13));
    WARN(std::format("translation (12.3, -7.1, 4.9) m: rel change V {:.2e} G {:.2e} Ke {:.2e} Ce {:.2e}", vError,
                     gError, keError, ceError));
}

TEST_CASE("ThermalElement_HugeOffsetTranslation", "[thermal][element][transform]") {
    // A modest tetrahedron a million metres from the origin: the edge
    // subtraction loses digits, and the loss is MEASURED and recorded rather
    // than assumed away. Double precision at 1e6 m resolves ~1e-10 m, so a
    // metre-sized element keeps ~10 digits; the bound asserted is loose.
    const Tet4Corners base = generalTet();
    const Tet4Corners far = transformed(base, Eigen::Matrix3d::Identity(), Eigen::Vector3d(1e6, 1e6, 1e6));
    const double keError = relativeDistance(productionKe(conductionOf(far, kK)), productionKe(conductionOf(base, kK)));
    const double vError = std::abs(geometryOf(far).volume().si() - geometryOf(base).volume().si()) /
                          geometryOf(base).volume().si();
    INFO("Ke " << keError << " V " << vError);
    CHECK(keError <= 1e-6);
    CHECK(vError <= 1e-6);
    WARN(std::format("offset 1e6 m: rel change V {:.2e}, Ke {:.2e} (double precision, recorded not asserted tight)",
                     vError, keError));
}

TEST_CASE("ThermalElement_RotationInvariant", "[thermal][element][transform]") {
    // A proper rotation, det R = +1, checked before use: V, Ke and Ce are
    // invariant for isotropic k, because G' = R G and R^T R = I.
    const Eigen::Matrix3d r = properRotation();
    REQUIRE((r.transpose() * r - Eigen::Matrix3d::Identity()).cwiseAbs().maxCoeff() <= 1e-15);
    REQUIRE_THAT(r.determinant(), WithinRel(1.0, 1e-14));

    const Tet4Corners base = generalTet();
    const Tet4Corners rotated = transformed(base, r, Eigen::Vector3d::Zero());
    const Tet4Geometry g0 = geometryOf(base);
    const Tet4Geometry g1 = geometryOf(rotated);
    const double vError = std::abs(g1.volume().si() - g0.volume().si()) / g0.volume().si();
    const double keError = relativeDistance(productionKe(conductionOf(rotated, kK)), productionKe(conductionOf(base, kK)));
    const double ceError = relativeDistance(productionCe(capacityOf(rotated, kRho, kCp)), productionCe(capacityOf(base, kRho, kCp)));
    // And G itself rotates: G' = R G.
    const double gError = relativeDistance(productionG(g1), Matrix34(r * productionG(g0)));
    INFO("V " << vError << " Ke " << keError << " Ce " << ceError << " G' - R G " << gError);
    CHECK(vError <= 1e-13);
    CHECK(keError <= 1e-13);
    CHECK(ceError <= 1e-13);
    CHECK(gError <= 1e-13);
    WARN(std::format("rotation Rz(31)Ry(-17)Rx(23), det R = {:.15g}: rel change V {:.2e} Ke {:.2e} Ce {:.2e}; |G' - R G| rel {:.2e}",
                     r.determinant(), vError, keError, ceError, gError));
}

TEST_CASE("ThermalElement_GradientRotatesWithGeometry", "[thermal][element][transform]") {
    // Transport an affine field rigidly: T'(x') = T(R^T x'), so the NODAL
    // values are the same at corresponding corners, and the gradient
    // transforms as grad' = R grad. Compared against R applied to the
    // hand-known gradient (7.2, -3.5, 11.4), not to production's.
    const Eigen::Matrix3d r = properRotation();
    const Tet4Corners base = generalTet();
    const Tet4Corners rotated = transformed(base, r, Eigen::Vector3d(0.5, -0.25, 1.0));
    const Tet4Temperatures t = affineField(base, 305.7, 7.2, -3.5, 11.4);
    const thermal::TemperatureGradient3 grad = geometryOf(rotated).gradientFrom(t);
    const Eigen::Vector3d expected = r * Eigen::Vector3d(7.2, -3.5, 11.4);
    const double error = (Eigen::Vector3d(grad.x.si(), grad.y.si(), grad.z.si()) - expected).cwiseAbs().maxCoeff();
    INFO("grad' " << grad.x.si() << " " << grad.y.si() << " " << grad.z.si() << " expected " << expected.transpose());
    CHECK(error <= 1e-11);
    WARN(std::format("gradient transport: |grad' - R (7.2, -3.5, 11.4)| = {:.2e} K/m", error));
}

TEST_CASE("ThermalElement_ReflectionIsNotARotation", "[thermal][element][transform]") {
    // det R = -1 reflects, and with the corner order kept fixed the signed
    // volume flips: REFUSED as inverted, which is why the rotation tests use
    // det R = +1 and why "invariant under any orthogonal R" would be a false
    // claim.
    Eigen::Matrix3d mirror = Eigen::Matrix3d::Identity();
    mirror(2, 2) = -1.0;
    REQUIRE_THAT(mirror.determinant(), WithinRel(-1.0, 1e-15));
    const Tet4Corners reflected = transformed(generalTet(), mirror, Eigen::Vector3d::Zero());
    CHECK(thermal::tet4GeometryProblem(reflected) ==
          std::optional<ElementProblem>{ElementProblem::InvertedElement});
    CHECK_FALSE(thermal::computeTet4Conduction(reflected, conductivity(kK)).has_value());
    // Reflection AND an odd reorder restore validity: the same volume, the
    // same Ke up to the permutation. Orientation is what matters.
    Tet4Corners restored = reflected;
    std::swap(restored[0], restored[1]);
    CHECK_FALSE(thermal::tet4GeometryProblem(restored).has_value());
}

TEST_CASE("ThermalElement_EvenPermutationCovariant", "[thermal][element][transform]") {
    // The 3-cycle sigma = (0 1 2): corners [p1, p2, p0, p3]. EVEN, so the
    // orientation is preserved and the matrices are permuted, not changed:
    // Ke'_ij = Ke_{sigma(i) sigma(j)}, i.e. Ke' = P Ke P^T with P_{i,sigma(i)}
    // = 1 -- built here independently, never compared raw.
    const std::array<std::size_t, 4> sigma{1, 2, 0, 3};
    const Tet4Corners base = generalTet();
    Tet4Corners permuted{};
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        permuted[i] = base[sigma[i]];
    }
    REQUIRE(meshing::signedVolume(permuted[0], permuted[1], permuted[2], permuted[3]).si() > 0.0);
    CHECK_FALSE(thermal::tet4GeometryProblem(permuted).has_value());

    Matrix4 p = Matrix4::Zero();
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        p(static_cast<int>(i), static_cast<int>(sigma[i])) = 1.0;
    }
    const Matrix4 ke = productionKe(conductionOf(base, kK));
    const Matrix4 kePerm = productionKe(conductionOf(permuted, kK));
    const Matrix4 ce = productionCe(capacityOf(base, kRho, kCp));
    const Matrix4 cePerm = productionCe(capacityOf(permuted, kRho, kCp));
    const double keError = relativeDistance(kePerm, Matrix4(p * ke * p.transpose()));
    const double ceError = relativeDistance(cePerm, Matrix4(p * ce * p.transpose()));
    INFO("Ke' - P Ke P^T " << keError << " Ce' - P Ce P^T " << ceError);
    CHECK(keError <= 1e-13);
    CHECK(ceError <= 1e-13);
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        for (std::size_t j = 0; j < kTet4Nodes; ++j) {
            CHECK_THAT(kePerm(static_cast<int>(i), static_cast<int>(j)),
                       WithinAbs(ke(static_cast<int>(sigma[i]), static_cast<int>(sigma[j])), 1e-13 * ke.cwiseAbs().maxCoeff()));
        }
    }
    CHECK_THAT(geometryOf(permuted).volume().si(), WithinRel(geometryOf(base).volume().si(), 1e-13));
    // And NOT equal raw, so the comparison is a real one.
    CHECK(relativeDistance(kePerm, ke) > 1e-3);
    WARN(std::format("even permutation (0 1 2): V unchanged; |Ke' - P Ke P^T| rel {:.2e}; |Ce' - P Ce P^T| rel {:.2e}; raw Ke differ by {:.2g}",
                     keError, ceError, relativeDistance(kePerm, ke)));
}

TEST_CASE("ThermalElement_OddPermutationRejected", "[thermal][element][transform]") {
    // Every transposition is odd: det J flips sign, and the element is
    // refused. No P^T Ke P is computed for it, by any path.
    const Tet4Corners base = generalTet();
    const double det0 = geometryOf(base).determinant().si();
    for (std::size_t a = 0; a < kTet4Nodes; ++a) {
        for (std::size_t b = a + 1; b < kTet4Nodes; ++b) {
            Tet4Corners swapped = base;
            std::swap(swapped[a], swapped[b]);
            INFO("swap " << a << " " << b);
            CHECK_THAT(meshing::signedVolume(swapped[0], swapped[1], swapped[2], swapped[3]).si() * 6.0,
                       WithinRel(-det0, 1e-12));
            CHECK(thermal::tet4GeometryProblem(swapped) ==
                  std::optional<ElementProblem>{ElementProblem::InvertedElement});
            CHECK_FALSE(thermal::computeTet4Geometry(swapped).has_value());
            CHECK_FALSE(thermal::computeTet4Conduction(swapped, conductivity(kK)).has_value());
            CHECK_FALSE(thermal::computeTet4Capacity(swapped, capacity(kRho, kCp)).has_value());
        }
    }
    WARN(std::format("odd permutations: all six transpositions flip det J = {:.6g} and are refused", det0));
}

// ===========================================================================
// Scaling
// ===========================================================================

TEST_CASE("ThermalElement_GeometryScalingConduction", "[thermal][element][scaling]") {
    // x' = s x: V' = s^3 V, G' = G/s, Ke' = s Ke, over engineering scales
    // 1e-3 m to 1e2 m. This is the test a hidden mm/m conversion fails.
    const Tet4Corners base = generalTet();
    const Tet4Geometry g0 = geometryOf(base);
    const Matrix4 ke0 = productionKe(conductionOf(base, kK));
    for (const double s : {1e-3, 0.1, 2.0, 10.0, 1e2}) {
        const Tet4Corners p = scaled(base, s);
        const Tet4Geometry g = geometryOf(p);
        const double vRatio = g.volume().si() / g0.volume().si();
        const double gRatio = productionG(g).cwiseAbs().maxCoeff() / productionG(g0).cwiseAbs().maxCoeff();
        const double keRatio = productionKe(conductionOf(p, kK)).cwiseAbs().maxCoeff() / ke0.cwiseAbs().maxCoeff();
        INFO("s " << s << " V " << vRatio << " G " << gRatio << " Ke " << keRatio);
        CHECK_THAT(vRatio, WithinRel(s * s * s, 1e-12));
        CHECK_THAT(gRatio, WithinRel(1.0 / s, 1e-12));
        CHECK_THAT(keRatio, WithinRel(s, 1e-12));
        CHECK(relativeDistance(productionKe(conductionOf(p, kK)), Matrix4(s * ke0)) <= 1e-12);
        CHECK(relativeDistance(productionG(g), Matrix34(productionG(g0) / s)) <= 1e-12);
        WARN(std::format("scale s = {:g}: V ratio {:.15g} (s^3 = {:g}), G ratio {:.15g} (1/s = {:g}), Ke ratio {:.15g} (s)",
                         s, vRatio, s * s * s, gRatio, 1.0 / s, keRatio));
    }
}

TEST_CASE("ThermalElement_GeometryScalingCapacity", "[thermal][element][scaling]") {
    // Ce' = s^3 Ce. Together with Ke' = s Ke this is the unit/formulation
    // cross-check: the two scale differently, and a kernel that got one
    // factor of length wrong cannot satisfy both.
    const Tet4Corners base = generalTet();
    const Matrix4 ce0 = productionCe(capacityOf(base, kRho, kCp));
    for (const double s : {1e-3, 0.1, 2.0, 10.0, 1e2}) {
        const Matrix4 ce = productionCe(capacityOf(scaled(base, s), kRho, kCp));
        const double ratio = ce.cwiseAbs().maxCoeff() / ce0.cwiseAbs().maxCoeff();
        INFO("s " << s << " Ce ratio " << ratio);
        CHECK_THAT(ratio, WithinRel(s * s * s, 1e-12));
        CHECK(relativeDistance(ce, Matrix4(s * s * s * ce0)) <= 1e-12);
        WARN(std::format("scale s = {:g}: Ce ratio {:.15g} (s^3 = {:g})", s, ratio, s * s * s));
    }
}

// ===========================================================================
// Determinism
// ===========================================================================

TEST_CASE("ThermalElement_RepeatEvaluationDeterministic", "[thermal][element][determinism]") {
    // 200 evaluations of the same element: bitwise identical, measured by
    // the classes' own equality over their stored doubles.
    const Tet4Corners p = generalTet();
    const Tet4Geometry g0 = geometryOf(p);
    const Tet4Conduction ke0 = conductionOf(p, kK);
    const Tet4Capacity ce0 = capacityOf(p, kRho, kCp);
    int differences = 0;
    for (int i = 0; i < 200; ++i) {
        if (!(geometryOf(p) == g0) || !(conductionOf(p, kK) == ke0) || !(capacityOf(p, kRho, kCp) == ce0)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    // And the classification is stable too.
    Tet4Corners inverted = p;
    std::swap(inverted[0], inverted[1]);
    for (int i = 0; i < 50; ++i) {
        CHECK(thermal::tet4GeometryProblem(inverted) == std::optional<ElementProblem>{ElementProblem::InvertedElement});
        CHECK_FALSE(thermal::tet4GeometryProblem(p).has_value());
    }
    WARN(std::format("determinism: 200 repeated evaluations, {} bitwise differences in V, G, Ke, Ce", differences));
}

TEST_CASE("ThermalElement_BitFingerprintForCrossPresetComparison", "[thermal][element][determinism]") {
    // A 64-bit FNV-1a hash over the BIT PATTERNS of V, det J, G, Ke and Ce of
    // 14 fixtures, printed so the three presets' outputs can be compared
    // exactly rather than to a tolerance. Equality across presets is
    // MEASURED, not promised: the hash is recorded either way, and the test
    // itself asserts only that it is reproducible within one process.
    const auto fingerprint = [] {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto mix = [&hash](double value) {
            const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
            for (int i = 0; i < 8; ++i) {
                hash ^= (bits >> (8 * i)) & 0xFFU;
                hash *= 1099511628211ULL;
            }
        };
        std::mt19937 engine(18'021);
        std::vector<Tet4Corners> fixtures = everyValidFixture();
        for (int i = 0; i < 10; ++i) {
            fixtures.push_back(randomTet(engine));
        }
        for (const Tet4Corners& p : fixtures) {
            const Tet4Geometry g = geometryOf(p);
            const Tet4Conduction ke = conductionOf(p, kK);
            const Tet4Capacity ce = capacityOf(p, kRho, kCp);
            mix(g.volume().si());
            mix(g.determinant().si());
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < kTet4Nodes; ++column) {
                    mix(g.g(row, column).si());
                }
            }
            for (std::size_t row = 0; row < kTet4ThermalDofs; ++row) {
                for (std::size_t column = 0; column < kTet4ThermalDofs; ++column) {
                    mix(ke(row, column).si());
                    mix(ce(row, column).si());
                }
            }
        }
        return std::pair{hash, fixtures.size()};
    };
    const auto [first, count] = fingerprint();
    const auto [second, countAgain] = fingerprint();
    CHECK(first == second);
    CHECK(count == countAgain);
    WARN(std::format("bit fingerprint over {} fixtures (V, det J, G, Ke, Ce): {:016x}", count, first));
}

TEST_CASE("ThermalElement_NoHiddenState", "[thermal][element][determinism]") {
    // Evaluate A, then B, then A again: the second A equals the first bit
    // for bit, so nothing from B leaked through static or cached state.
    const Tet4Corners a = generalTet();
    const Tet4Corners b = skewTet();
    const Tet4Conduction first = conductionOf(a, kK);
    const Tet4Capacity firstCe = capacityOf(a, kRho, kCp);
    [[maybe_unused]] const Tet4Conduction other = conductionOf(b, 3.0 * kK);
    [[maybe_unused]] const Tet4Capacity otherCe = capacityOf(b, 2.0 * kRho, 0.5 * kCp);
    CHECK(conductionOf(a, kK) == first);
    CHECK(capacityOf(a, kRho, kCp) == firstCe);
    CHECK_FALSE(conductionOf(b, 3.0 * kK) == first);
}

// ===========================================================================
// Integration: the mesh, the DOF model, the material
// ===========================================================================

TEST_CASE("ThermalElement_CornersFromMeshInElementOrder", "[thermal][element][integration]") {
    // Sparse handles, a tetrahedron stored as [9001, 7, 500000, 104]: the
    // corners come back in THAT order, the same order P18-DOF-001's
    // elementDofs() returns the unknowns in, so local row i is unknown i.
    meshing::MeshBuilder builder;
    const std::array<std::pair<NodeId::ValueType, Point3D>, 4> nodes{{
        {7, at(1.7, 0.3, 0.2)},
        {104, at(0.1, 0.2, 2.0)},
        {9001, at(0.2, -0.4, 0.1)},
        {500000, at(0.4, 1.5, 0.6)},
    }};
    for (const auto& [handle, position] : nodes) {
        REQUIRE(builder.addNode(NodeId::fromValue(handle), position).has_value());
    }
    const std::array<NodeId, 4> order{NodeId::fromValue(9001), NodeId::fromValue(7), NodeId::fromValue(500000),
                                      NodeId::fromValue(104)};
    const Result<meshing::ElementId> element = builder.addTetrahedron(order, meshing::RegionId::fromValue(1));
    REQUIRE(element.has_value());
    const meshing::Mesh mesh = builder.build();
    const meshing::Tetrahedron* tet = mesh.findTetrahedron(*element);
    REQUIRE(tet != nullptr);

    const Result<Tet4Corners> corners = thermal::cornersOf(mesh, *tet);
    REQUIRE(corners.has_value());
    // Local order [9001, 7, 500000, 104] is the general fixture's corners
    // [a, b, c, d], so the element is positively oriented as stored.
    const Tet4Corners expected = generalTet();
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        CHECK((*corners)[i] == expected[i]);
    }

    // The DOF map's local order is the same element order.
    const Result<thermal::ThermalDofMap> map = thermal::buildThermalDofMap(mesh);
    REQUIRE(map.has_value());
    const Result<std::array<thermal::ThermalDofIndex, 4>> dofs = map->elementDofs(*tet);
    REQUIRE(dofs.has_value());
    for (std::size_t i = 0; i < kTet4Nodes; ++i) {
        const Result<NodeId> node = map->nodeOf((*dofs)[i]);
        REQUIRE(node.has_value());
        CHECK(*node == order[i]);
    }

    // The element is valid in that order (positive volume), and its matrix
    // is 4x4 whatever the handles are.
    CHECK_FALSE(thermal::tet4GeometryProblem(*corners).has_value());
    const Tet4Conduction ke = conductionOf(*corners, kK);
    CHECK(ke(3, 3).si() > 0.0);
    CHECK(ke(4, 0).si() == 0.0);
    CHECK(ke(0, 4).si() == 0.0);

    SECTION("a tetrahedron naming a node the mesh does not have is refused") {
        meshing::Tetrahedron foreign = *tet;
        foreign.nodes[2] = NodeId::fromValue(8);
        const Result<Tet4Corners> refused = thermal::cornersOf(mesh, foreign);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::NotFound);
        CHECK_THAT(refused.error().message, ContainsSubstring("node:8"));
    }
}

TEST_CASE("ThermalElement_LocalDofsAreOneScalarPerNode", "[thermal][element][integration]") {
    // 4 x 4, by the constants, and tied to P18-DOF-001's kThermalDofsPerNode
    // and P16's arity without the kernel including either for it.
    STATIC_CHECK(kTet4Nodes == 4);
    STATIC_CHECK(kTet4ThermalDofs == 4);
    STATIC_CHECK(kTet4ThermalDofs == thermal::kThermalDofsPerNode * kTet4Nodes);
    CHECK(meshing::nodeCount(meshing::ElementType::Tetrahedron4) == kTet4Nodes);
    const Tet4Conduction ke = conductionOf(generalTet(), kK);
    const Tet4Capacity ce = capacityOf(generalTet(), kRho, kCp);
    for (std::size_t i = 0; i < 6; ++i) {
        for (std::size_t j = 0; j < 6; ++j) {
            const bool inside = i < 4 && j < 4;
            CHECK((ke(i, j).si() != 0.0) == (inside && (i == j || std::abs(ke(i, j).si()) > 0.0)));
            CHECK((ce(i, j).si() != 0.0) == inside);
        }
    }
}

TEST_CASE("ThermalElement_SteadyMaterialIntegration", "[thermal][element][integration]") {
    // A P18-MAT-001 material resolved for a STEADY analysis carries k and no
    // capacity. Ke is formed from it; nothing on the steady path asks for
    // rho or cp, and the resolved material does not have them to give.
    MaterialPart part;
    part.assign("k_only", kK, std::nullopt, std::nullopt);
    const ThermalMaterial material = part.resolve(thermal::ThermalAnalysisMode::Steady);
    CHECK_FALSE(material.capacity().has_value());
    const Reference ref = reference(generalTet(), kK, kRho, kCp);
    const Result<Tet4Conduction> ke = thermal::computeTet4Conduction(generalTet(), material.conductivity());
    REQUIRE(ke.has_value());
    CHECK(relativeDistance(productionKe(*ke), ref.ke) <= 1e-13);
    WARN("steady material (k only, no rho, no cp): Ke formed; capacity() empty, never asked for");
}

TEST_CASE("ThermalElement_TransientMaterialIntegration", "[thermal][element][integration]") {
    // k, rho and cp from one resolved transient material, no copied
    // database: Ke and Ce both form, Ce from the material's own capacity
    // pair, and Ce's total is rho cp V -- not k V.
    MaterialPart part;
    part.assign("steel_ish", kK, kRho, kCp);
    const ThermalMaterial material = part.resolve(thermal::ThermalAnalysisMode::Transient);
    REQUIRE(material.capacity().has_value());
    const Tet4Geometry g = geometryOf(generalTet());
    const Reference ref = reference(generalTet(), kK, kRho, kCp);
    const Result<Tet4Conduction> ke = thermal::computeTet4Conduction(g, material.conductivity());
    const Result<Tet4Capacity> ce = thermal::computeTet4Capacity(g, *material.capacity());
    REQUIRE(ke.has_value());
    REQUIRE(ce.has_value());
    CHECK(relativeDistance(productionKe(*ke), ref.ke) <= 1e-13);
    CHECK(relativeDistance(productionCe(*ce), ref.ce) <= 1e-13);
    CHECK_THAT(ce->total().si(), WithinRel(kRho * kCp * g.volume().si(), 1e-13));
    CHECK(std::abs(ce->total().si() - kK * g.volume().si()) > 0.9 * kRho * kCp * g.volume().si());
    // rho cp as the material derives it equals what the matrix used.
    CHECK_THAT(ce->total().si(), WithinRel(material.capacity()->volumetric().si() * g.volume().si(), 1e-13));
    WARN("transient material (k, rho, cp): Ke and Ce formed from one resolved material");
}

TEST_CASE("ThermalElement_FieldSwapProtection", "[thermal][element][integration]") {
    // k, rho and cp are distinctive values, so k in Ce or rho/cp in Ke would
    // be a different number. Ke's scale is k V |grad|^2; Ce's is rho cp V.
    const Tet4Corners p = generalTet();
    const double v = geometryOf(p).volume().si();
    const Matrix4 ke = productionKe(conductionOf(p, kK));
    const Matrix4 ce = productionCe(capacityOf(p, kRho, kCp));
    const Matrix4 keWithRho = productionKe(conductionOf(p, kRho));
    const Matrix4 keWithCp = productionKe(conductionOf(p, kCp));
    CHECK(relativeDistance(ke, keWithRho) > 0.5);
    CHECK(relativeDistance(ke, keWithCp) > 0.5);
    CHECK_THAT(ce.sum(), WithinRel(kRho * kCp * v, 1e-13));
    CHECK(std::abs(ce.sum() - kK * kCp * v) > 0.5 * kRho * kCp * v);
    CHECK(std::abs(ce.sum() - kK * kRho * v) > 0.5 * kRho * kCp * v);
    // rho and cp swapped in a hand-built pair give the same PRODUCT, so Ce
    // cannot see that swap by value -- which is why the pair is a struct of
    // two distinct Quantity types and the swap is a compile-fail case
    // (RHO_AND_CP_ARE_NOT_INTERCHANGEABLE), not a runtime check.
    CHECK(relativeDistance(productionCe(capacityOf(p, kCp, kRho)), ce) <= 1e-15);
}

TEST_CASE("ThermalElement_EveryElementProblemHasAName", "[thermal][element][integration]") {
    CHECK(thermal::toString(ElementProblem::NonFiniteCoordinate) == "non_finite_coordinate");
    CHECK(thermal::toString(ElementProblem::DegenerateElement) == "degenerate_element");
    CHECK(thermal::toString(ElementProblem::InvertedElement) == "inverted_element");
    CHECK(thermal::toString(ElementProblem::NonFiniteResult) == "non_finite_result");
    CHECK(thermal::toString(ElementProblem::InvalidConductivity) == "invalid_conductivity");
    CHECK(thermal::toString(ElementProblem::InvalidDensity) == "invalid_density");
    CHECK(thermal::toString(ElementProblem::InvalidSpecificHeat) == "invalid_specific_heat");
}

TEST_CASE("ThermalElement_DimensionalAnalysis", "[thermal][element][integration]") {
    // The header's static_asserts, restated where a reader looks for them,
    // and the runtime consequence: the same element in the same metres
    // through two literal spellings gives the same matrices.
    STATIC_CHECK(decltype(Volume{} * ThermalConductivity{} * InverseLength{} * InverseLength{})::dimension ==
                 ThermalConductance::dimension);
    STATIC_CHECK(decltype(Density{} * SpecificHeatCapacity{} * Volume{})::dimension == HeatCapacity::dimension);
    STATIC_CHECK(decltype(ThermalConductance{} * Temperature{})::dimension == Power::dimension);
    STATIC_CHECK(ThermalConductance::dimension != HeatCapacity::dimension);
    // 1700 mm and 1.7 m are the same corner; Ke and Ce are the same.
    const Tet4Corners inMetres = generalTet();
    const Tet4Corners viaMillimetres{
        Point3D{200_mm, -400_mm, 100_mm}, Point3D{1700_mm, 300_mm, 200_mm},
        Point3D{400_mm, 1500_mm, 600_mm}, Point3D{100_mm, 200_mm, 2000_mm}};
    CHECK(relativeDistance(productionKe(conductionOf(viaMillimetres, kK)), productionKe(conductionOf(inMetres, kK))) <= 1e-14);
    CHECK(relativeDistance(productionCe(capacityOf(viaMillimetres, kRho, kCp)), productionCe(capacityOf(inMetres, kRho, kCp))) <= 1e-14);
}
