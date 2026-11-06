/// \file
/// P19-GRAD-001 — finite-volume gradient reconstruction, ADR-070.
///
/// WHAT IS CHECKED WHERE, AND WHY THERE.
///
/// ```text
/// the identity          sum_f C_f (x) S_f = V I, on every reference body. It is
///                       the reason Green-Gauss works at all, so it is asserted
///                       on real meshes rather than taken from the ADR
/// the analytic pair     |d| = 5/4, d . n = 3/4, cos(theta) = 3/5, all exact.
///                       Where a value is pinned against closed-form arithmetic
/// the orthogonal pair   d exactly parallel to n, so the two forms of a
///                       ZeroGradient least-squares row AGREE -- which is why
///                       Decision 5 cannot be tested here, and is tested on the
///                       analytic pair instead
/// the one-cell tet      four boundary facets and no internal face: the only
///                       fixture where the boundary contribution is the WHOLE
///                       answer
/// the reference bodies  linear exactness, covariance, determinism and the
///                       skewness comparison, on meshes nobody designed to
///                       flatter the operator
/// ```
///
/// THE ORACLES ARE WRITTEN OUT AGAIN rather than read from the implementation --
/// WITH ONE EXCEPTION THAT TWO REVIEWS CAUGHT, NOW CORRECTED. The volume
/// identity, the slip-wall projection, the Green-Gauss surface sum with both
/// candidate weights, and the tensor transformation law each appear here in
/// their own arithmetic, so a transposition in production cannot be mirrored
/// here.
///
/// The conditioning oracle was NOT independent: it was a line-for-line
/// transcription of `eigenvalueRatio`, which would have mirrored precisely the
/// transposition it existed to exclude. It is a cyclic JACOBI diagonalisation
/// now, which shares no arithmetic with the closed form under test.
///
/// AND THREE ORACLES SHARE ONE THING WITH PRODUCTION ON PURPOSE: they read
/// `topology.owner(face)` and apply the same winding convention. So they kill
/// any mutation of the sign, and they would NOT reveal a shared misunderstanding
/// of the convention itself. `cellSurfaceVectorSum` is the independent authority
/// there, and the constant-mode case is where it is consulted.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;

using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdBoundaryPolicy;
using cfd::CfdGeometry;
using cfd::CfdGradientInput;
using cfd::CfdGradientMethod;
using cfd::CfdGradientProblem;
using cfd::CfdGradientSummary;
using cfd::CfdPressureGradientField;
using cfd::CfdResolvedTarget;
using cfd::CfdTopology;
using cfd::CfdVelocityGradientField;
using cfd::CfdVelocityGradientInput;
using cfd::FaceIndex;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PreparedCfdBoundary;
using cfd::PressureGradient;
using cfd::PressureOutlet;
using cfd::SlipWall;
using cfd::VectorComponent;
using cfd::VelocityGradient;
using cfd::Velocity3;
using meshing::ElementId;
using meshing::NodeId;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

using Vec = std::array<double, 3>;
/// A 3x3, row-major, for the velocity gradient tensor and its transformation.
using Mat = std::array<double, 9>;

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// The relative error a LINEAR field may show on a hand-built fixture.
///
/// Least squares' error is bounded by its normal matrix's condition number times
/// eps. The hand fixtures are two or one cell, where that ratio is of order ten
/// (measured and reported by the conditioning tests below), so eps * 10 is about
/// 2e-15 and this is five hundred times it. The margin is for the libm `hypot`
/// in the magnitudes, which is not required to be identically rounded between
/// builds.
constexpr double kExactOnAFixture = 1.0e-12;

/// The relative error a LINEAR field may show on a REFERENCE body.
///
/// Same bound, different conditioning: the eight reference meshes reach an
/// extreme-eigenvalue ratio of order 1e5 (measured, and reported by
/// `TheWorstConditioningIsReportedAndEveryReferenceBodyIsWithinTheLimit`), so
/// the predicted worst is 1e5 * 2.2e-16 = 2.2e-11 and this is about fifty times
/// it. It is the same 1e-9 the thermal suite uses for an assembled mesh, and for
/// the same reason: the bound is asserted over thousands of cells, and a ninth
/// model would widen the spread of cell shapes.
constexpr double kExactOnAReferenceBody = 1.0e-9;

/// How many units of `eps * valueScale / lengthScale` a reconstructed gradient
/// may be out by, where the two scales are the size of the cell values that
/// cancel and the smallest distance the fit divides by.
///
/// THE BOUND IS IN UNITS OF THE PRODUCTS, NOT OF THE ANSWER. A least-squares fit
/// works on DIFFERENCES of cell values, so a large constant offset is
/// represented with an absolute error of order `eps * |phi|`, their difference
/// carries twice that, and the gradient is that difference over a distance. The
/// resulting error has nothing to do with the size of the gradient, so a bound
/// stated relative to the gradient is the mistake P18-ELEM-001 recorded as its
/// finding F4 -- and this author made it here first, with a 1e-12 relative bound
/// that measured 4.16e-11 at an offset of 1e6.
///
/// MEASURED, THEN BOUNDED: the worst reading over every case in this file is
/// about one unit, reported by the cases that assert it. 64 is the same factor
/// P19-DIFF-001 settled on for the same kind of bound.
constexpr double kGradientProductUnits = 64.0;

/// The four faces of a tetrahedron, wound outward for a positively oriented node
/// order. The same table `CfdDiffusionTests.cpp` and `CfdBoundaryTests.cpp` use.
constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

using FaceUse = std::pair<std::size_t, std::array<std::size_t, 3>>;

[[nodiscard]] std::map<std::array<std::size_t, 3>, FaceUse>
faceUses(const std::vector<std::array<std::size_t, 4>>& tets) {
    std::map<std::array<std::size_t, 3>, FaceUse> uses;
    for (const auto& tet : tets) {
        for (const auto& face : kFaces) {
            std::array<std::size_t, 3> wound{tet[face[0]], tet[face[1]], tet[face[2]]};
            std::array<std::size_t, 3> key = wound;
            std::ranges::sort(key);
            auto found = uses.find(key);
            if (found == uses.end()) {
                uses.emplace(key, FaceUse{1, wound});
            } else {
                ++found->second.first;
            }
        }
    }
    return uses;
}

[[nodiscard]] meshing::Mesh buildMesh(const std::vector<Point3D>& points,
                                      const std::vector<std::array<std::size_t, 4>>& tets) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> nodes;
    for (const Point3D& p : points) {
        nodes.push_back(require(builder.addNode(p)));
    }
    for (const auto& tet : tets) {
        REQUIRE(builder.addTetrahedron({nodes[tet[0]], nodes[tet[1]], nodes[tet[2]], nodes[tet[3]]},
                                       kRegion));
    }
    for (const auto& [key, use] : faceUses(tets)) {
        const auto& [count, wound] = use;
        if (count == 1) {
            REQUIRE(builder.addTriangle({nodes[wound[0]], nodes[wound[1]], nodes[wound[2]]}, kRegion));
        }
    }
    return builder.build();
}

/// Two cells sharing one face on `x + y + z = 10 mm`, where `d` is EXACTLY
/// parallel to the face normal.
[[nodiscard]] std::vector<Point3D> orthogonalPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm},  Point3D{10_mm, 0_mm, 0_mm},  Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}, Point3D{10_mm, 10_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};

/// One cell: four boundary faces and no internal face.
[[nodiscard]] std::vector<Point3D> oneCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kOneCell{{0, 1, 2, 3}};

/// The analytic pair, in METRES: `|d| = 5/4`, `d . n = 3/4`, `cos(theta) = 3/5`,
/// all exact. NOT orthogonal, which is what makes it the fixture that can tell
/// ADR-070 Decision 5's two forms apart.
[[nodiscard]] std::vector<Point3D> analyticPoints() {
    const auto p = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    return {p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0, 0, -1), p(0, 4, 2)};
}
const std::vector<std::array<std::size_t, 4>> kAnalyticPair{{0, 2, 1, 3}, {0, 1, 2, 4}};

[[nodiscard]] std::vector<meshing::MappedFace> onePatch(const meshing::Mesh& mesh) {
    meshing::MappedFace face;
    face.index = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        face.facets.push_back(triangle.id);
    }
    return {face};
}

[[nodiscard]] FaceName someFace(std::uint64_t feature = 1) {
    return FaceName{ObjectId::fromValue(feature), FaceSelector{.role = FaceRole::EndCap}};
}

// ---------------------------------------------------------------------------
// Small vector and matrix arithmetic, written out here
// ---------------------------------------------------------------------------

[[nodiscard]] Vec siOf(const Point3D& p) { return {p.x.si(), p.y.si(), p.z.si()}; }
[[nodiscard]] Vec siOf(const AreaVector3D& v) { return {v.x.si(), v.y.si(), v.z.si()}; }
[[nodiscard]] Vec siOf(const Translation3D& v) { return {v.x.si(), v.y.si(), v.z.si()}; }

[[nodiscard]] double dotOf(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
[[nodiscard]] double normOf(const Vec& a) { return std::hypot(a[0], a[1], a[2]); }
[[nodiscard]] Vec subOf(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

/// `R v`, for a row-major 3x3.
[[nodiscard]] Vec applyTo(const Mat& r, const Vec& v) {
    return {r[0] * v[0] + r[1] * v[1] + r[2] * v[2], r[3] * v[0] + r[4] * v[1] + r[5] * v[2],
            r[6] * v[0] + r[7] * v[1] + r[8] * v[2]};
}

/// `A B`, row-major.
[[nodiscard]] Mat multiplyOf(const Mat& a, const Mat& b) {
    Mat out{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += a[3 * i + k] * b[3 * k + j];
            }
            out[3 * i + j] = sum;
        }
    }
    return out;
}

[[nodiscard]] Mat transposeOf(const Mat& a) {
    return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]};
}

[[nodiscard]] double worstEntryOf(const Mat& a) {
    double worst = 0.0;
    for (const double value : a) {
        worst = std::max(worst, std::abs(value));
    }
    return worst;
}

/// @p mesh with every node moved by @p motion, and the SAME connectivity in the
/// SAME order, so the topology of the two is identical index for index and the
/// gradients can be compared one to one.
///
/// A REMESHED ROTATED BODY COULD NOT BE COMPARED THIS WAY. P16 does not promise
/// the same tetrahedralisation in a rotated frame, which the thermal reference
/// suite records; the transform is therefore applied node by node, exactly as
/// `CfdGeometryTests.cpp` does for the same reason.
[[nodiscard]] meshing::Mesh transformed(const meshing::Mesh& mesh, const RigidTransform3D& motion) {
    meshing::MeshBuilder builder;
    for (const meshing::Node& node : mesh.nodes()) {
        REQUIRE(builder.addNode(node.id, motion.apply(node.position)));
    }
    // ONE ASCENDING PASS OVER BOTH KINDS, because MeshBuilder requires strictly
    // increasing element handles and P16's canonical order is every Triangle3
    // then every Tetrahedron4 -- which is NOT ascending in the handle when a
    // mesh holds both.
    std::size_t t = 0;
    std::size_t q = 0;
    const std::span<const meshing::Triangle> triangles = mesh.triangles();
    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
    while (t < triangles.size() || q < tets.size()) {
        const bool takeTriangle =
            q == tets.size() || (t < triangles.size() && triangles[t].id < tets[q].id);
        if (takeTriangle) {
            REQUIRE(builder.addTriangle(triangles[t].nodes, triangles[t].region));
            ++t;
        } else {
            REQUIRE(builder.addTetrahedron(tets[q].nodes, tets[q].region));
            ++q;
        }
    }
    return builder.build();
}

/// A mesh, its topology and its metrics, all through the plain builders, so the
/// operator can be shown configurations a document cannot produce.
struct Hand {
    meshing::Mesh mesh;
    CfdTopology topology;
    CfdGeometry geometry;

    [[nodiscard]] static Hand make(const std::vector<Point3D>& points,
                                   const std::vector<std::array<std::size_t, 4>>& tets) {
        meshing::Mesh mesh = buildMesh(points, tets);
        Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
        INFO((geometry.has_value() ? std::string{} : geometry.error().message));
        REQUIRE(geometry.has_value());
        return Hand{std::move(mesh), std::move(*topology), std::move(*geometry)};
    }

    [[nodiscard]] std::vector<ElementId> facets() const {
        std::vector<ElementId> out;
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            out.push_back(topology.triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        return out;
    }

    [[nodiscard]] Point3D insidePoint() const {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(0));
        REQUIRE(centroid.has_value());
        return *centroid;
    }

    /// A boundary where every facet carries @p condition.
    [[nodiscard]] PreparedCfdBoundary uniform(const CfdBoundaryCondition& condition) const {
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(std::ranges::adjacent_find(ascending) == ascending.end());
        const CfdResolvedTarget target{condition, ascending};
        const std::array<CfdResolvedTarget, 1> targets{target};
        // ADR-067 Decision 7 refuses BOTH arms of the pressure datum, so the
        // datum is passed exactly when the condition's own pressure policy is
        // not FixedValue -- read off the table rather than decided here.
        const bool fixesPressure =
            cfd::treatmentOf(condition.kind()).pressure == CfdBoundaryPolicy::FixedValue;
        const std::optional<cfd::CfdReferencePressure> datum =
            fixesPressure ? std::nullopt
                          : std::optional<cfd::CfdReferencePressure>{
                                cfd::CfdReferencePressure{.value = 0_Pa, .location = insidePoint()}};
        Result<PreparedCfdBoundary> bound =
            cfd::bindCfdBoundary(topology, geometry, mesh, targets, 0, datum, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        return std::move(*bound);
    }
};

struct FacetCondition {
    ElementId facet{};
    CfdBoundaryCondition condition;
};

[[nodiscard]] PreparedCfdBoundary bindPerFacet(const Hand& hand,
                                               std::span<const FacetCondition> assignment,
                                               bool needsDatum) {
    std::vector<CfdResolvedTarget> targets;
    for (const FacetCondition& one : assignment) {
        targets.push_back(CfdResolvedTarget{one.condition, {one.facet}});
    }
    std::optional<cfd::CfdReferencePressure> datum;
    if (needsDatum) {
        datum = cfd::CfdReferencePressure{.value = 0_Pa, .location = hand.insidePoint()};
    }
    Result<PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, targets, 0, datum, std::nullopt, std::nullopt);
    INFO((bound.has_value() ? std::string{} : bound.error().message));
    REQUIRE(bound.has_value());
    return std::move(*bound);
}

/// The boundary facet whose three nodes all satisfy @p onPlane, found from the
/// MESH rather than from the geometry's normals, so a fixture does not depend on
/// the accessor a test then asserts about.
template <typename Predicate>
[[nodiscard]] ElementId facetWhere(const Hand& hand, Predicate onPlane) {
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
        const FaceIndex face = hand.topology.faceOf(slot);
        const std::array<NodeId, 3> nodes = hand.topology.faceNodes(face);
        bool all = true;
        for (const NodeId id : nodes) {
            const meshing::Node* node = hand.mesh.findNode(id);
            REQUIRE(node != nullptr);
            all = all && onPlane(node->position);
        }
        if (all) {
            return hand.topology.triangleOf(slot);
        }
    }
    FAIL("no boundary facet lies in the requested plane");
    return ElementId{};
}

// ---------------------------------------------------------------------------
// Oracles, written out again
// ---------------------------------------------------------------------------

/// THE INTERPOLATION WEIGHT ORACLE: the weight on the OWNER's value, by the
/// normal projection. Written out here so the operator's own form cannot be
/// mirrored.
[[nodiscard]] double weightOracle(const CfdGeometry& geometry, FaceIndex face) {
    const double total = geometry.orthogonalDistance(face).si();
    const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
    REQUIRE(normal.has_value());
    const Vec unitNormal{normal->x(), normal->y(), normal->z()};
    const double owner = dotOf(siOf(geometry.cellToFace(face)), unitNormal);
    return (total - owner) / total;
}

/// THE SLIP-WALL PROJECTION ORACLE: `U_b = U_P - (U_P . n) n`.
[[nodiscard]] Vec slipOracle(const Vec& velocity, const Vec& unitNormal) {
    const double along = dotOf(velocity, unitNormal);
    return {velocity[0] - unitNormal[0] * along, velocity[1] - unitNormal[1] * along,
            velocity[2] - unitNormal[2] * along};
}

/// A linear field `phi(x) = offset + g . x`, as cell values in SI.
[[nodiscard]] std::vector<double> linearCellValues(const CfdGeometry& geometry, const Vec& gradient,
                                                   double offset) {
    std::vector<double> values(geometry.cells(), 0.0);
    for (std::size_t c = 0; c < geometry.cells(); ++c) {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(c));
        REQUIRE(centroid.has_value());
        values[c] = offset + dotOf(gradient, siOf(*centroid));
    }
    return values;
}

/// A velocity field `u(x) = G x + c`, COMPONENT-BLOCKED as ADR-066 lays it out.
[[nodiscard]] std::vector<double> linearVelocity(const CfdGeometry& geometry, const Mat& tensor,
                                                 const Vec& constant) {
    const std::size_t cells = geometry.cells();
    std::vector<double> values(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(c));
        REQUIRE(centroid.has_value());
        const Vec at = applyTo(tensor, siOf(*centroid));
        for (std::size_t i = 0; i < 3; ++i) {
            values[i * cells + c] = at[i] + constant[i];
        }
    }
    return values;
}

[[nodiscard]] CfdGradientInput pressureInput(std::span<const double> values, CfdGradientMethod method) {
    CfdGradientInput input;
    input.method = method;
    input.cellValue = values;
    return input;
}

[[nodiscard]] CfdVelocityGradientInput velocityInput(std::span<const double> values,
                                                     VectorComponent component,
                                                     CfdGradientMethod method) {
    CfdVelocityGradientInput input;
    input.method = method;
    input.component = component;
    input.currentVelocity = values;
    return input;
}

/// The three rows of the reconstructed velocity gradient tensor, as a row-major
/// 3x3 in SI.
[[nodiscard]] Mat velocityTensor(const CfdTopology& topology, const CfdGeometry& geometry,
                                 const PreparedCfdBoundary& boundary, std::span<const double> values,
                                 CfdGradientMethod method, CellIndex cell) {
    Mat out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
            topology, geometry, boundary, velocityInput(values, cfd::kVectorComponents[i], method));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        const std::optional<CfdVelocityGradientField::Vector> row = field->at(cell);
        REQUIRE(row.has_value());
        out[3 * i + 0] = row->x.si();
        out[3 * i + 1] = row->y.si();
        out[3 * i + 2] = row->z.si();
    }
    return out;
}

} // namespace

// ===========================================================================
// THE IDENTITY GREEN-GAUSS RESTS ON
// ===========================================================================

TEST_CASE("CfdGradient_TheSurfaceCentroidOuterProductIsTheVolumeIdentityOnEveryFixture", "[cfd][grad]") {
    // WHY THIS IS THE FIRST TEST IN THE FILE. For an exact linear field,
    // Green-Gauss with the TRUE value at each face centroid gives
    //
    //     (1/V) sum_f phi(C_f) S_f = (1/V) [ a sum_f S_f + sum_f (g . C_f) S_f ]
    //
    // and the first term vanishes by ADR-065 Decision 2's closure identity while
    // the second is `V g` by THIS one. So Green-Gauss is linear-exact on ANY
    // mesh when it is given the true face value, and every departure from
    // exactness it shows belongs to the INTERPOLATION instead. That is the
    // finding ADR-070 Decision 2 rests on, and it is asserted here on real
    // geometry rather than taken from the ADR's own 20,000-sample measurement.
    struct Case {
        const char* name;
        std::vector<Point3D> points;
        std::vector<std::array<std::size_t, 4>> tets;
    };
    const std::array<Case, 3> cases{{{"orthogonal pair", orthogonalPoints(), kTwoCells},
                                     {"analytic pair", analyticPoints(), kAnalyticPair},
                                     {"one cell", oneCellPoints(), kOneCell}}};

    double worst = 0.0;
    for (const Case& one : cases) {
        INFO(one.name);
        const Hand hand = Hand::make(one.points, one.tets);
        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            INFO("cell " << c);
            // sum_f C_f (x) S_f, accumulated here, with the sign read from
            // `owner`.
            //
            // THIS MUCH IS NOT INDEPENDENT OF PRODUCTION, and the first version
            // of this comment implied it was. Reading the sign from `owner` is
            // what the operator does too, so if the winding convention itself
            // were misunderstood both would be wrong together and the identity
            // would still come out as `V I`. What IS independent is
            // `cellSurfaceVectorSum`, which ADR-065 made the authority on the
            // sign, and which the constant-mode case checks against; and the
            // mutation that ignores `owner` entirely, which the sweep kills.
            // This case carries the IDENTITY, not the convention.
            Mat accumulated{};
            double scale = 0.0;
            for (const FaceIndex face : hand.topology.cellFaces(cell)) {
                const std::optional<Point3D> centroid = hand.geometry.faceCentroid(face);
                REQUIRE(centroid.has_value());
                const Vec cf = siOf(*centroid);
                const Vec stored = siOf(hand.geometry.faceAreaVector(face));
                const bool isOwner = hand.topology.owner(face).value() == c;
                const Vec outward = isOwner ? stored : Vec{-stored[0], -stored[1], -stored[2]};
                for (std::size_t i = 0; i < 3; ++i) {
                    for (std::size_t j = 0; j < 3; ++j) {
                        accumulated[3 * i + j] += cf[i] * outward[j];
                    }
                }
                scale += normOf(cf) * normOf(outward);
            }
            const double volume = hand.geometry.cellVolume(cell).si();
            REQUIRE(volume > 0.0);
            for (std::size_t i = 0; i < 3; ++i) {
                for (std::size_t j = 0; j < 3; ++j) {
                    const double expected = i == j ? volume : 0.0;
                    const double deviation = std::abs(accumulated[3 * i + j] - expected) / scale;
                    worst = std::max(worst, deviation);
                }
            }
        }
    }
    INFO("worst relative deviation from V I: " << worst);
    // The bound is in units of the PRODUCTS the sum accumulates, not of the
    // volume: it is a cancellation of `C_f . S_f` terms, so its error scales
    // with their size and not with the answer. P18-ELEM-001's finding F4.
    CHECK(worst < 64.0 * std::numeric_limits<double>::epsilon());
}

namespace {

void assignWaterTo(Document& document) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> id = features::createMaterial(document, "CfdWater", water);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// A reference body, meshed, with its topology and its metrics.
struct Reference {
    meshref::MeshedReference model;
    Result<cfd::CfdModel> input;
    Result<cfd::CfdMeshContract> contract;
    Result<CfdTopology> topology;
    Result<CfdGeometry> geometry;

    explicit Reference(bettercad::reference::MeshReferenceModelKind kind) : Reference(document(kind)) {}

    explicit Reference(Document built)
        : model(std::move(built)), input(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})),
          contract(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})),
          topology(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})),
          geometry(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})) {
        assignWaterTo(model.document());
        (void)model.regenerate();
        (void)model.require();
        input = cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
        INFO((input.has_value() ? std::string{} : input.error().message));
        REQUIRE(input.has_value());
        contract = cfd::requireCfdMesh(*input);
        REQUIRE(contract.has_value());
        topology = cfd::requireCfdTopology(*input, *contract);
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        geometry = cfd::requireCfdGeometry(*input, *topology);
        INFO((geometry.has_value() ? std::string{} : geometry.error().message));
        REQUIRE(geometry.has_value());
    }

    static Document document(bettercad::reference::MeshReferenceModelKind kind) {
        Result<Document> built = bettercad::reference::buildMeshReferenceModel(kind);
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    [[nodiscard]] std::vector<ElementId> facets() const {
        std::vector<ElementId> ascending;
        for (std::size_t b = 0; b < topology->boundaryFaces(); ++b) {
            ascending.push_back(topology->triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        return ascending;
    }

    /// Every facet a no-slip wall: the velocity is FixedValue at zero and the
    /// pressure is ZeroGradient, so a reference pressure is required.
    [[nodiscard]] PreparedCfdBoundary wallEverywhere() const {
        const CfdResolvedTarget target{
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}}, facets()};
        const std::array<CfdResolvedTarget, 1> targets{target};
        const std::optional<Point3D> centroid = geometry->cellCentroid(CellIndex::fromValue(0));
        REQUIRE(centroid.has_value());
        const cfd::CfdReferencePressure datum{.value = 0_Pa, .location = *centroid};
        Result<PreparedCfdBoundary> bound =
            cfd::bindCfdBoundary(*topology, *geometry, input->mesh().mesh(), targets, 0, datum,
                                 std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        return std::move(*bound);
    }
};

/// A reference body's boundary with one condition per facet.
///
/// NEEDED BECAUSE A LINEAR FIELD'S DIRICHLET VALUE VARIES OVER THE SURFACE. A
/// single condition carries one value, so pinning linear exactness against a
/// consistent boundary needs one condition per facet -- which is a configuration
/// a document cannot produce, and is exactly what the plain binder is for.
[[nodiscard]] PreparedCfdBoundary bindReferencePerFacet(const Reference& body,
                                                        std::span<const FacetCondition> assignment) {
    std::vector<CfdResolvedTarget> targets;
    targets.reserve(assignment.size());
    for (const FacetCondition& one : assignment) {
        targets.push_back(CfdResolvedTarget{one.condition, {one.facet}});
    }
    // Every facet fixes the pressure, so ADR-067 Decision 7 REFUSES a datum.
    Result<PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(*body.topology, *body.geometry, body.input->mesh().mesh(), targets, 0,
                             std::nullopt, std::nullopt, std::nullopt);
    INFO((bound.has_value() ? std::string{} : bound.error().message));
    REQUIRE(bound.has_value());
    return std::move(*bound);
}

/// A hand fixture's boundary whose prescribed pressure at every facet is the
/// value a linear field takes at that facet's centroid.
[[nodiscard]] PreparedCfdBoundary agreeingPressureBoundary(const Hand& hand, const Vec& gradient,
                                                           double offset) {
    std::vector<FacetCondition> assignment;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
        const std::optional<Point3D> centroid = hand.geometry.faceCentroid(hand.topology.faceOf(slot));
        REQUIRE(centroid.has_value());
        const double atFacet = offset + dotOf(gradient, siOf(*centroid));
        assignment.push_back(FacetCondition{
            hand.topology.triangleOf(slot),
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(b + 1),
                                 PressureOutlet{someFace(b + 1), Pressure::fromSi(atFacet)}}});
    }
    return bindPerFacet(hand, assignment, false);
}

/// The same for a reference body.
[[nodiscard]] PreparedCfdBoundary agreeingReferenceBoundary(const Reference& body, const Vec& gradient,
                                                            double offset) {
    std::vector<FacetCondition> assignment;
    for (std::size_t b = 0; b < body.topology->boundaryFaces(); ++b) {
        const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
        const std::optional<Point3D> centroid = body.geometry->faceCentroid(body.topology->faceOf(slot));
        REQUIRE(centroid.has_value());
        const double atFacet = offset + dotOf(gradient, siOf(*centroid));
        assignment.push_back(FacetCondition{
            body.topology->triangleOf(slot),
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(b + 1),
                                 PressureOutlet{someFace(b + 1), Pressure::fromSi(atFacet)}}});
    }
    return bindReferencePerFacet(body, assignment);
}

/// A reconstructed pressure gradient measured against the linear field's own
/// gradient, with the two scales a bound has to be stated in.
///
/// WHY AN ABSOLUTE ERROR AND TWO SCALES, AND NOT JUST A RELATIVE ONE. The fit
/// works on DIFFERENCES of cell values, so when a large constant offset is
/// added, each value carries an absolute representation error of order
/// `eps * |phi|` and their difference carries twice that -- while the gradient
/// it determines is the difference divided by a distance. The error in the
/// answer is therefore of order
///
///     eps * valueScale / lengthScale
///
/// which has nothing to do with the size of the gradient. A bound stated
/// relative to the ANSWER is the mistake P18-ELEM-001 recorded as finding F4,
/// and this author made it here first: the relative error at an offset of 1e6
/// measured 4.16e-11 against a 1e-12 bound, which is the predicted cancellation
/// behaving exactly as it should and not a defect. The bound below is stated in
/// units of the products instead.
struct Reading {
    /// `|g - g_exact| / |g_exact|`, for reporting.
    double worstError = 0.0;
    /// `|g - g_exact|`, which is what the bound applies to.
    double worstAbsolute = 0.0;
    /// `max |phi|` over the cells: the size of the numbers that cancel.
    double valueScale = 0.0;
    /// The smallest `|d|` the fit divides by, over every face of the mesh.
    double lengthScale = 0.0;
    CfdGradientSummary summary{};

    /// The error, in units of `eps * valueScale / lengthScale`.
    [[nodiscard]] double inUnitsOfTheProducts() const {
        const double unit =
            std::numeric_limits<double>::epsilon() * valueScale / std::max(lengthScale, 1.0e-300);
        return worstAbsolute / std::max(unit, 1.0e-300);
    }
};

/// The smallest distance the fit divides by: the owner-to-neighbour vector on an
/// internal face and the cell-to-face vector on a boundary face.
[[nodiscard]] double smallestFitLength(const CfdTopology& topology, const CfdGeometry& geometry) {
    double smallest = std::numeric_limits<double>::infinity();
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        if (topology.isInternal(face)) {
            const std::optional<Translation3D> span = geometry.ownerToNeighbour(face);
            REQUIRE(span.has_value());
            smallest = std::min(smallest, normOf(siOf(*span)));
        } else {
            smallest = std::min(smallest, normOf(siOf(geometry.cellToFace(face))));
        }
    }
    REQUIRE(std::isfinite(smallest));
    REQUIRE(smallest > 0.0);
    return smallest;
}

[[nodiscard]] Reading readPressureGradient(const CfdTopology& topology, const CfdGeometry& geometry,
                                           const PreparedCfdBoundary& boundary, const Vec& gradient,
                                           double offset, CfdGradientMethod method) {
    const std::vector<double> values = linearCellValues(geometry, gradient, offset);
    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        topology, geometry, boundary, pressureInput(values, method));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    Reading reading;
    reading.summary = field->summary();
    reading.lengthScale = smallestFitLength(topology, geometry);
    for (const double value : values) {
        reading.valueScale = std::max(reading.valueScale, std::abs(value));
    }
    for (std::size_t c = 0; c < field->cells(); ++c) {
        const std::optional<CfdPressureGradientField::Vector> g = field->at(CellIndex::fromValue(c));
        REQUIRE(g.has_value());
        const Vec got{g->x.si(), g->y.si(), g->z.si()};
        const double absolute = normOf(subOf(got, gradient));
        reading.worstAbsolute = std::max(reading.worstAbsolute, absolute);
        reading.worstError = std::max(reading.worstError, absolute / normOf(gradient));
    }
    return reading;
}

} // namespace

// ===========================================================================
// CONSTANT-FIELD NULL BEHAVIOUR
// ===========================================================================

TEST_CASE("CfdGradient_AUniformPressureReconstructsToExactlyZeroByLeastSquares", "[cfd][grad]") {
    // BIT-EXACT, AND STRUCTURALLY SO. Every least-squares right-hand side is a
    // DIFFERENCE of cell values, so a uniform field makes every one of them
    // exactly `0.0` and the solve returns exactly zero whatever the normal
    // matrix holds. `WithinAbs` would pass a result that is merely small, which
    // is a weaker claim than the one being made.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    // A pressure outlet at 0 Pa, so the prescribed value AGREES with the
    // interior constant -- which is what makes this a uniform field rather than
    // one with a jump at the wall.
    //
    // THE CONSTANT IS NEITHER ZERO NOR DYADIC, AND BOTH PROPERTIES WERE FORCED BY
    // A MUTATION. At `0.0` the unstable interpolation
    // `w phi_P + (1 - w) phi_N` returns exactly `0.0` too, so a constant-field
    // test at zero cannot see it. Raising the constant to 17.25 did not help
    // either, and the reason is sharper: 17.25 is 69/4, so both products are
    // EXACT and their sum is exactly the constant whatever the weight. A search
    // over 200,000 random constants at this fixture's weight of 2/3 found that
    // 16 % of them do distinguish the two forms, and this is one of them --
    // `12345.6789` comes back as `12345.678900000003` through the unstable form.
    constexpr double kConstant = 12345.6789;
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                          PressureOutlet{someFace(), Pressure::fromSi(kConstant)}});

    const std::vector<double> uniform(hand.topology.cells(), kConstant);
    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(uniform, CfdGradientMethod::LeastSquares));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    for (std::size_t c = 0; c < field->cells(); ++c) {
        INFO("cell " << c);
        const std::optional<CfdPressureGradientField::Vector> g = field->at(CellIndex::fromValue(c));
        REQUIRE(g.has_value());
        CHECK(g->x.si() == 0.0);
        CHECK(g->y.si() == 0.0);
        CHECK(g->z.si() == 0.0);
    }
    CHECK(field->largestMagnitude().si() == 0.0);
    CHECK(field->summary().constantModeIsExact);
}

TEST_CASE("CfdGradient_AUniformVelocityComponentReconstructsToExactlyZeroByLeastSquares", "[cfd][grad]") {
    // THE SAME CLAIM DOWN THE OTHER COLUMN, and it needs its own case because
    // the velocity reads a component-blocked array through a different policy
    // column. A no-slip wall prescribes zero, which agrees with a zero field.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    const std::vector<double> uniform(3 * hand.topology.cells(), 0.0);
    for (const VectorComponent component : cfd::kVectorComponents) {
        INFO(cfd::toString(component));
        const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
            hand.topology, hand.geometry, boundary,
            velocityInput(uniform, component, CfdGradientMethod::LeastSquares));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        CHECK(field->largestMagnitude().si() == 0.0);
        CHECK(field->summary().constantModeIsExact);
    }
}

TEST_CASE("CfdGradient_AUniformFieldWithADisagreeingWallIsNotAConstantField", "[cfd][grad]") {
    // THE QUALIFIER THAT MAKES THE CLAIM ABOVE HONEST, asserted rather than
    // written in a comment. A Dirichlet wall prescribing a DIFFERENT value than
    // the interior constant describes a field that is not uniform: it is
    // constant inside with a jump at the wall, and its true gradient is NOT
    // zero. An operator returning zero here would be wrong, so "a constant
    // field gives zero" WITHOUT the qualifier is a claim this suite must not
    // make.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 500_Pa}});

    const std::vector<double> interior(hand.topology.cells(), 0.0);
    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(interior, CfdGradientMethod::LeastSquares));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    // Not zero and not small: a 500 Pa jump across a 10 mm cell.
    INFO("largest |grad p| = " << field->largestMagnitude().si() << " Pa/m");
    CHECK(field->largestMagnitude().si() > 1.0e4);
    // And the flag STILL says the operator has an exact constant mode, because
    // the flag is a statement about the operator and the mesh and not about this
    // input. Both are true at once, which is why the flag is documented the way
    // it is -- and asserting it here is what stops a reader taking the flag for
    // a promise about every input.
    CHECK(field->summary().constantModeIsExact);
}

TEST_CASE("CfdGradient_TheGreenGaussConstantModeFlagIsMeasuredFromTheClosureAndNotAssumed", "[cfd][grad]") {
    // The flag for Green-Gauss claims `sum_f S_f` is BIT-ZERO at every cell.
    // That is a property of the mesh, so it is computed here independently, from
    // `cellSurfaceVectorSum` -- which ADR-065 made the authority on the sign each
    // face is taken with -- and compared against what the operator reported. A
    // flag hard-coded `true` for Green-Gauss would survive a test that only
    // checked the uniform result was small.
    //
    // TWO THINGS THE MUTATION SWEEP FORCED INTO THIS CASE. A version using a
    // constant of `0.0` could not see the unstable interpolation, because zero
    // is the one value where it agrees with the stable form. And a version
    // running only on the hand fixtures could not see the flag hard-coded
    // `true`, because every hand fixture has a bit-zero closure, so
    // `true == true` held everywhere. The constant is now non-zero and the
    // REFERENCE BODIES are included, where ADR-065's own evidence records the
    // cancellation as bit-exact on only two of the eight -- so both arms of the
    // equality are reached, and that is asserted rather than hoped for.
    constexpr double kConstant = 12345.6789;

    const auto readFlag = [&](const CfdTopology& topology, const CfdGeometry& geometry,
                              const PreparedCfdBoundary& boundary) {
        bool bitZeroEverywhere = true;
        for (std::size_t c = 0; c < topology.cells(); ++c) {
            const std::optional<AreaVector3D> sum =
                geometry.cellSurfaceVectorSum(CellIndex::fromValue(c));
            REQUIRE(sum.has_value());
            bitZeroEverywhere =
                bitZeroEverywhere && sum->x.si() == 0.0 && sum->y.si() == 0.0 && sum->z.si() == 0.0;
        }
        const std::vector<double> uniform(topology.cells(), kConstant);
        const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
            topology, geometry, boundary, pressureInput(uniform, CfdGradientMethod::GreenGauss));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        INFO("independently measured bit-zero closure " << bitZeroEverywhere << ", reported "
                                                        << field->summary().constantModeIsExact);
        CHECK(field->summary().constantModeIsExact == bitZeroEverywhere);
        if (bitZeroEverywhere) {
            // Where the closure IS bit-zero a uniform field must give exactly
            // zero, because the result is `phi * sum_f S_f / V`.
            CHECK(field->largestMagnitude().si() == 0.0);
        }
        return bitZeroEverywhere;
    };

    struct Case {
        const char* name;
        std::vector<Point3D> points;
        std::vector<std::array<std::size_t, 4>> tets;
    };
    const std::array<Case, 3> cases{{{"orthogonal pair", orthogonalPoints(), kTwoCells},
                                     {"analytic pair", analyticPoints(), kAnalyticPair},
                                     {"one cell", oneCellPoints(), kOneCell}}};
    std::size_t exactClosure = 0;
    std::size_t inexactClosure = 0;

    for (const Case& one : cases) {
        INFO(one.name);
        const Hand hand = Hand::make(one.points, one.tets);
        const PreparedCfdBoundary boundary =
            hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                              PressureOutlet{someFace(), Pressure::fromSi(kConstant)}});
        if (readFlag(hand.topology, hand.geometry, boundary)) {
            ++exactClosure;
        } else {
            ++inexactClosure;
        }
    }

    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        std::vector<FacetCondition> assignment;
        for (std::size_t b = 0; b < body.topology->boundaryFaces(); ++b) {
            const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
            assignment.push_back(FacetCondition{
                body.topology->triangleOf(slot),
                CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(b + 1),
                                     PressureOutlet{someFace(b + 1), Pressure::fromSi(kConstant)}}});
        }
        const PreparedCfdBoundary boundary = bindReferencePerFacet(body, assignment);
        if (readFlag(*body.topology, *body.geometry, boundary)) {
            ++exactClosure;
        } else {
            ++inexactClosure;
        }
    }

    // BOTH ARMS MUST BE REACHED, or the equality above is satisfied by one
    // constant comparing equal to itself everywhere. This is the assertion a
    // hard-coded `true` dies on, and it is the one the sweep showed was missing.
    INFO("fixtures with a bit-zero closure: " << exactClosure << ", without: " << inexactClosure);
    CHECK(exactClosure >= 1);
    CHECK(inexactClosure >= 1);
}

// ===========================================================================
// LINEAR-FIELD EXACTNESS
// ===========================================================================

TEST_CASE("CfdGradient_ALinearPressureIsReconstructedExactlyByLeastSquaresOnTheAnalyticPair",
          "[cfd][grad]") {
    // THE CENTRAL CLAIM. Least squares fits the gradient to the neighbour values
    // at their own centroids, so for an exactly linear field every residual
    // `phi_N - phi_P - g . d_N` is identically zero at the true gradient and the
    // fit reproduces it WHATEVER the weighting -- which is why ADR-070 Decision 6
    // can say the weights cannot affect linear exactness.
    //
    // The fixture is the NON-ORTHOGONAL analytic pair, deliberately: on the
    // orthogonal pair a defect in a boundary row's DIRECTION would not show,
    // because there `C_f - C_P` is parallel to `n`.
    //
    // THE BOUNDARY AGREES WITH THE FIELD. A wall's pressure policy is
    // ZeroGradient, whose row says `g . n = 0`, and a general linear field does
    // not satisfy that -- so a wall boundary would make the operator fit a
    // constraint the field violates, and the test would measure the mismatch
    // rather than the method. One pressure outlet per facet, each prescribing
    // the field's own value there, is the configuration that asks the right
    // question.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const Vec gradient{7.2, -3.5, 11.4};
    // THE OFFSET IS LARGE ON PURPOSE. P18-ELEM-001's finding F4: a large
    // constant multiplies the terms that must cancel, so a bound stated relative
    // to the ANSWER would pass an operator that does not cancel them. P19-DIFF-001
    // measured that at an offset of 305.7 a plain relative bound passes anyway,
    // which is why its own offset was raised to 1e6.
    const double offset = 1.0e6;
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, offset);

    const Reading reading = readPressureGradient(hand.topology, hand.geometry, boundary, gradient, offset,
                                                 CfdGradientMethod::LeastSquares);
    INFO("worst relative error at offset 1e6: " << reading.worstError << ", absolute "
                                                 << reading.worstAbsolute << ", in units of eps * "
                                                 << reading.valueScale << " / " << reading.lengthScale
                                                 << ": " << reading.inUnitsOfTheProducts());
    CHECK(reading.inUnitsOfTheProducts() < kGradientProductUnits);
    // Every facet fixes the pressure, so the census must say so -- which also
    // confirms the fixture really is the configuration the comment describes.
    CHECK(reading.summary.fixedValueFaces == hand.topology.boundaryFaces());
    CHECK(reading.summary.zeroGradientFaces == 0);
    CHECK(reading.summary.symmetryFaces == 0);
}

TEST_CASE("CfdGradient_ALargeConstantOffsetDoesNotDisturbLinearExactness", "[cfd][grad]") {
    // THE OFFSET SWEEP THAT JUSTIFIES THE ONE ABOVE. If the error were
    // independent of the offset the large value would be decoration; if it grew
    // without bound the method would be useless. It grows with the offset, which
    // is subtractive cancellation behaving as expected, and stays inside the
    // bound at 1e6.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const Vec gradient{7.2, -3.5, 11.4};
    double atZero = -1.0;
    double atMillion = -1.0;
    for (const double offset : {0.0, 305.7, 1.0e3, 1.0e6}) {
        INFO("offset " << offset);
        const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, offset);
        const Reading reading = readPressureGradient(hand.topology, hand.geometry, boundary, gradient,
                                                     offset, CfdGradientMethod::LeastSquares);
        INFO("absolute error " << reading.worstAbsolute << ", relative " << reading.worstError
                               << ", in units of the products " << reading.inUnitsOfTheProducts());
        // THE BOUND HOLDS AT EVERY OFFSET, in units of the products.
        CHECK(reading.inUnitsOfTheProducts() < kGradientProductUnits);
        if (offset == 0.0) {
            atZero = reading.worstAbsolute;
        }
        if (offset == 1.0e6) {
            atMillion = reading.worstAbsolute;
        }
    }
    // AND THE ABSOLUTE ERROR REALLY DOES GROW WITH THE OFFSET, which is what
    // makes the large offset a test rather than decoration: if it did not grow,
    // nothing would be cancelling and finding F4 would not apply here.
    INFO("absolute error at offset 0: " << atZero << ", at 1e6: " << atMillion);
    CHECK(atMillion > 1.0e3 * std::max(atZero, std::numeric_limits<double>::denorm_min()));
}

TEST_CASE("CfdGradient_TheThreeVelocityComponentsGiveThreeDifferentGradients", "[cfd][grad]") {
    // ADR-066'S LAYOUT, MADE OBSERVABLE. The field is `u = G x + c` with a
    // tensor whose three rows differ, so a component read at a fixed offset, or
    // read cell-blocked instead of component-blocked, returns another row's
    // gradient and this case fails.
    //
    // IT DOES NOT CLAIM EXACTNESS, and the name says so. On a two-cell fixture
    // every cell touches the boundary, and a no-slip wall's velocity row
    // prescribes zero where the linear field is not zero -- so the fit is
    // answering a different question than `grad u = G`. Exactness for the
    // velocity column is claimed on a reference body, where interior cells
    // exist, by `...VelocityGradientTensorIsCovariant...` and the pressure
    // cases; here the claim is only that the component selector works.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    const Mat tensor{0.7, -1.3, 2.1, 5.5, 0.9, -3.3, -2.2, 4.4, 1.1};
    const std::vector<double> values = linearVelocity(hand.geometry, tensor, Vec{0.0, 0.0, 0.0});

    std::array<Vec, 3> rows{};
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("component " << i);
        const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
            hand.topology, hand.geometry, boundary,
            velocityInput(values, cfd::kVectorComponents[i], CfdGradientMethod::LeastSquares));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        const std::optional<CfdVelocityGradientField::Vector> g = field->at(CellIndex::fromValue(0));
        REQUIRE(g.has_value());
        rows[i] = Vec{g->x.si(), g->y.si(), g->z.si()};
    }

    // THE THREE ROWS MUST DIFFER. If the component were read at a fixed offset,
    // or cell-blocked instead of component-blocked, two of these would coincide.
    INFO("rows: " << rows[0][0] << " " << rows[1][0] << " " << rows[2][0]);
    CHECK(normOf(subOf(rows[0], rows[1])) > 1.0);
    CHECK(normOf(subOf(rows[1], rows[2])) > 1.0);
    CHECK(normOf(subOf(rows[0], rows[2])) > 1.0);
}

TEST_CASE("CfdGradient_ALinearFieldIsReconstructedExactlyByLeastSquaresOnEveryReferenceBody",
          "[cfd][grad]") {
    // THE CLAIM THAT MATTERS, on meshes nobody designed to flatter it. ADR-065
    // measured skewness up to 6.5 and ADR-063 non-orthogonality to nearly 90
    // degrees on these bodies; least squares is linear-exact anyway, which is
    // the whole reason ADR-070 Decision 2 chose it.
    std::size_t bodies = 0;
    double worstOverAll = 0.0;
    double worstConditioning = 0.0;
    const Vec gradient{7.2, -3.5, 11.4};

    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            // The one model with no body, and ASSERTED to be that one: a filter
            // that quietly grew would make this sweep smaller than it claims.
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        const PreparedCfdBoundary boundary = agreeingReferenceBoundary(body, gradient, 0.0);
        const Reading reading = readPressureGradient(*body.topology, *body.geometry, boundary, gradient,
                                                     0.0, CfdGradientMethod::LeastSquares);
        INFO("worst relative error " << reading.worstError << ", conditioning "
                                     << reading.summary.worstConditioning << ", cells "
                                     << reading.summary.cells);
        CHECK(reading.inUnitsOfTheProducts() < kGradientProductUnits);
        CHECK(reading.worstError < kExactOnAReferenceBody);
        CHECK(reading.summary.cells == body.topology->cells());
        worstOverAll = std::max(worstOverAll, reading.worstError);
        worstConditioning = std::max(worstConditioning, reading.summary.worstConditioning);
    }
    INFO("bodies " << bodies << ", worst relative error " << worstOverAll << ", worst conditioning "
                   << worstConditioning);
    // THE BODY COUNT IS ASSERTED AGAINST AN INDEPENDENT FIGURE, so a loop that
    // silently meshed none would fail rather than pass vacuously -- the defect
    // P19-IC-001's review found in a block that called itself discriminating.
    CHECK(bodies == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(bodies == 8);
}

TEST_CASE("CfdGradient_GreenGaussIsExactWhereTheFaceIsUnskewedAndIsNotWhereItIsSkewed", "[cfd][grad]") {
    // THE MEASUREMENT ADR-070 DECISION 2 RESTS ON, asserted in BOTH directions.
    //
    // Green-Gauss with the true face value is linear-exact on any mesh -- the
    // first test in this file proves the identity that makes it so. What breaks
    // it is the INTERPOLATION, and the interpolation is exact exactly when the
    // face is not skewed. So this case REQUIRES exactness where skewness is zero
    // and REQUIRES a measurable failure where it is not. An operator somehow
    // exact in both places would contradict the identity, and this test would
    // catch that too.
    const Vec gradient{7.2, -3.5, 11.4};

    SECTION("an orthogonal, unskewed internal face: Green-Gauss is exact") {
        const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
        // THE PREMISE IS ASSERTED, not assumed: the internal face's skewness
        // must actually be zero, or this section proves nothing.
        const std::optional<double> skew = hand.geometry.skewness(FaceIndex::fromValue(0));
        REQUIRE(skew.has_value());
        INFO("the internal face's skewness: " << *skew);
        REQUIRE(std::abs(*skew) < 1.0e-12);

        const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 0.0);
        const Reading reading = readPressureGradient(hand.topology, hand.geometry, boundary, gradient, 0.0,
                                                     CfdGradientMethod::GreenGauss);
        INFO("Green-Gauss relative error at zero skewness: " << reading.worstError);
        CHECK(reading.worstError < kExactOnAFixture);
        CHECK(reading.summary.worstSkewness < 1.0e-12);
    }

    SECTION("a skewed internal face: Green-Gauss is measurably inexact, least squares is not") {
        const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
        const std::optional<double> skew = hand.geometry.skewness(FaceIndex::fromValue(0));
        REQUIRE(skew.has_value());
        INFO("the internal face's skewness: " << *skew);
        REQUIRE(std::abs(*skew) > 1.0e-3);

        const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 0.0);
        const Reading gauss = readPressureGradient(hand.topology, hand.geometry, boundary, gradient, 0.0,
                                                   CfdGradientMethod::GreenGauss);
        const Reading squares = readPressureGradient(hand.topology, hand.geometry, boundary, gradient, 0.0,
                                                     CfdGradientMethod::LeastSquares);
        INFO("Green-Gauss " << gauss.worstError << " against least squares " << squares.worstError
                            << " at skewness " << *skew);

        // THE FAILURE IS REQUIRED, not tolerated: were Green-Gauss exact here,
        // the identity test's reasoning would be wrong.
        CHECK(gauss.worstError > 1.0e-6);
        CHECK(squares.worstError < kExactOnAFixture);
        CHECK(squares.worstError < gauss.worstError);

        // The reported skewness tracks the geometry's own reading for the method
        // that interpolates, and is exactly zero for the one that does not.
        CHECK_THAT(gauss.summary.worstSkewness, WithinRel(std::abs(*skew), 1.0e-12));
        CHECK(squares.summary.worstSkewness == 0.0);
    }
}

TEST_CASE("CfdGradient_LeastSquaresBeatsGreenGaussOnEveryReferenceBody", "[cfd][grad]") {
    // THE ANSWER TO "skew mesh behaviour", measured as the DIFFERENCE between two
    // implementations on the same mesh rather than stated as a sentence. This is
    // why ADR-070 Decision 2 kept Green-Gauss instead of implementing least
    // squares alone: with one method there is nothing to difference.
    const Vec gradient{7.2, -3.5, 11.4};
    std::size_t bodies = 0;
    std::size_t squaresWon = 0;
    double worstPenalty = 0.0;

    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            // The one model with no body, and ASSERTED to be that one: a filter
            // that quietly grew would make this sweep smaller than it claims.
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        const PreparedCfdBoundary boundary = agreeingReferenceBoundary(body, gradient, 0.0);
        const Reading gauss = readPressureGradient(*body.topology, *body.geometry, boundary, gradient, 0.0,
                                                   CfdGradientMethod::GreenGauss);
        const Reading squares = readPressureGradient(*body.topology, *body.geometry, boundary, gradient,
                                                     0.0, CfdGradientMethod::LeastSquares);
        INFO("Green-Gauss " << gauss.worstError << " against least squares " << squares.worstError
                            << ", worst skewness " << gauss.summary.worstSkewness);
        CHECK(squares.worstError < kExactOnAReferenceBody);
        if (squares.worstError < gauss.worstError) {
            ++squaresWon;
        }
        worstPenalty = std::max(worstPenalty, gauss.worstError);
        // THE SKEWNESS IS REAL ON THESE MESHES, which is what makes the
        // comparison meaningful rather than a tie between two exact methods.
        CHECK(gauss.summary.worstSkewness > 0.0);
        CHECK(squares.summary.worstSkewness == 0.0);
        // And the two methods are distinguishable in the summary itself.
        CHECK(gauss.summary.method == CfdGradientMethod::GreenGauss);
        CHECK(squares.summary.method == CfdGradientMethod::LeastSquares);
        CHECK(gauss.summary.worstConditioning == 0.0);
        CHECK(squares.summary.worstConditioning >= 1.0);
    }
    INFO("bodies " << bodies << ", least squares strictly better on " << squaresWon
                   << ", worst Green-Gauss error " << worstPenalty);
    CHECK(bodies == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(squaresWon == bodies);
}

namespace {

/// A length within rounding of zero, for finding an axis-aligned facet.
[[nodiscard]] bool nearZeroLength(Length value) { return std::abs(value.si()) < 1.0e-12; }

/// A hand fixture's boundary prescribing @p byBoundaryFace at each facet, in
/// `BoundaryFaceIndex` order.
///
/// NEEDED FOR COVARIANCE. The moved mesh must carry the SAME PHYSICAL boundary
/// values as the original, and its connectivity is identical index for index, so
/// the values are indexed rather than recomputed from the moved geometry -- which
/// would quietly re-derive them and hide a transport error.
[[nodiscard]] PreparedCfdBoundary prescribedPressureBoundary(const Hand& hand,
                                                             std::span<const double> byBoundaryFace) {
    REQUIRE(byBoundaryFace.size() == hand.topology.boundaryFaces());
    std::vector<FacetCondition> assignment;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
        assignment.push_back(FacetCondition{
            hand.topology.triangleOf(slot),
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(b + 1),
                                 PressureOutlet{someFace(b + 1),
                                                Pressure::fromSi(byBoundaryFace[b])}}});
    }
    return bindPerFacet(hand, assignment, false);
}

/// The rotation this file uses for covariance: a translation AFTER a rotation
/// about an off-origin axis along no body direction.
///
/// COMPOUND AND NON-AXIS-ALIGNED ON PURPOSE, which is the reason
/// `CfdGeometryTests.cpp` gives for its own: a defect that permutes axes, or
/// transposes the matrix, cannot survive a rotation that mixes all three.
[[nodiscard]] RigidTransform3D covarianceMotion() {
    return RigidTransform3D::translation(Translation3D{-31_mm, 17_mm, 5_mm})
        .after(RigidTransform3D::rotation(
            Axis3D{Point3D{2_mm, -3_mm, 1_mm}, *Direction3D::fromComponents(1.0, 2.0, -2.0)},
            Angle::fromSi(2.3)));
}

} // namespace

// ===========================================================================
// RIGID-TRANSFORM COVARIANCE
// ===========================================================================

TEST_CASE("CfdGradient_ThePressureGradientIsCovariantUnderARigidTransform", "[cfd][grad]") {
    // THE CLEANEST FORM OF THE CLAIM, and it needs no transported field: an
    // affine motion maps a centroid to the moved centroid exactly, because a
    // centroid is the MEAN of the nodes. So the same physical field takes the
    // SAME VALUE at each cell of both meshes, and the cell-value array is
    // literally reused. Only the geometry moved, so only the geometry can rotate
    // the answer.
    //
    // `grad p` is a vector, so it must come back as `R grad p` -- WITHOUT the
    // translation, which a gradient cannot see.
    const Hand original = Hand::make(analyticPoints(), kAnalyticPair);
    const RigidTransform3D motion = covarianceMotion();
    const meshing::Mesh movedMesh = transformed(original.mesh, motion);

    Result<CfdTopology> movedTopology = cfd::buildCfdTopology(movedMesh, onePatch(movedMesh));
    INFO((movedTopology.has_value() ? std::string{} : movedTopology.error().message));
    REQUIRE(movedTopology.has_value());
    Result<CfdGeometry> movedGeometry = cfd::buildCfdGeometry(movedMesh, *movedTopology);
    REQUIRE(movedGeometry.has_value());
    const Hand moved{movedMesh, std::move(*movedTopology), std::move(*movedGeometry)};

    // THE TWO MESHES AGREE ON EVERY INDEX, which is what makes a one-to-one
    // comparison legitimate. A remeshed rotated body would not: P16 promises no
    // such thing, and the thermal reference suite records that.
    REQUIRE(moved.topology.cells() == original.topology.cells());
    REQUIRE(moved.topology.faces() == original.topology.faces());
    REQUIRE(moved.topology.internalFaces() == original.topology.internalFaces());

    // THE TRANSFORM REALLY MOVES THINGS -- the guard CfdGeometryTests.cpp uses,
    // because a covariance test against an identity transform passes for the
    // wrong reason.
    double worstMotion = 0.0;
    for (std::size_t c = 0; c < original.topology.cells(); ++c) {
        const std::optional<Point3D> before = original.geometry.cellCentroid(CellIndex::fromValue(c));
        const std::optional<Point3D> after = moved.geometry.cellCentroid(CellIndex::fromValue(c));
        REQUIRE(before.has_value());
        REQUIRE(after.has_value());
        worstMotion = std::max(worstMotion, normOf(subOf(siOf(*after), siOf(*before))));
    }
    INFO("the worst centroid displacement: " << worstMotion << " m");
    REQUIRE(worstMotion > 0.01);

    const Vec gradient{7.2, -3.5, 11.4};
    const double offset = 305.7;
    // The cell values: computed once, on the ORIGINAL geometry, and reused.
    const std::vector<double> values = linearCellValues(original.geometry, gradient, offset);
    // And the facet values, likewise computed once from the original geometry.
    std::vector<double> facetValues;
    for (std::size_t b = 0; b < original.topology.boundaryFaces(); ++b) {
        const std::optional<Point3D> centroid =
            original.geometry.faceCentroid(original.topology.faceOf(BoundaryFaceIndex::fromValue(b)));
        REQUIRE(centroid.has_value());
        facetValues.push_back(offset + dotOf(gradient, siOf(*centroid)));
    }

    const Mat rotation = motion.matrix();
    const Vec expected = applyTo(rotation, gradient);

    for (const CfdGradientMethod method : cfd::kCfdGradientMethods) {
        INFO(cfd::toString(method));
        const PreparedCfdBoundary before = prescribedPressureBoundary(original, facetValues);
        const PreparedCfdBoundary after = prescribedPressureBoundary(moved, facetValues);

        const Result<CfdPressureGradientField> was = cfd::reconstructCfdPressureGradient(
            original.topology, original.geometry, before, pressureInput(values, method));
        const Result<CfdPressureGradientField> is = cfd::reconstructCfdPressureGradient(
            moved.topology, moved.geometry, after, pressureInput(values, method));
        INFO((was.has_value() ? std::string{} : was.error().message));
        REQUIRE(was.has_value());
        INFO((is.has_value() ? std::string{} : is.error().message));
        REQUIRE(is.has_value());

        double worst = 0.0;
        for (std::size_t c = 0; c < was->cells(); ++c) {
            INFO("cell " << c);
            const std::optional<CfdPressureGradientField::Vector> a = was->at(CellIndex::fromValue(c));
            const std::optional<CfdPressureGradientField::Vector> b = is->at(CellIndex::fromValue(c));
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            const Vec gotBefore{a->x.si(), a->y.si(), a->z.si()};
            const Vec gotAfter{b->x.si(), b->y.si(), b->z.si()};
            // COVARIANT: the moved gradient is the rotated original gradient.
            const Vec rotated = applyTo(rotation, gotBefore);
            const double scale = std::max(normOf(gotBefore), 1.0e-300);
            worst = std::max(worst, normOf(subOf(gotAfter, rotated)) / scale);
            // INVARIANT: the magnitude, which a rotation cannot change.
            CHECK_THAT(normOf(gotAfter), WithinRel(normOf(gotBefore), kExactOnAFixture));
        }
        INFO("worst relative covariance error: " << worst);
        CHECK(worst < kExactOnAFixture);
        // And least squares, being exact, must land on the rotated exact answer
        // rather than merely agreeing with itself.
        if (method == CfdGradientMethod::LeastSquares) {
            const std::optional<CfdPressureGradientField::Vector> b = is->at(CellIndex::fromValue(0));
            REQUIRE(b.has_value());
            const Vec gotAfter{b->x.si(), b->y.si(), b->z.si()};
            INFO("expected R g = " << expected[0] << " " << expected[1] << " " << expected[2]);
            CHECK(normOf(subOf(gotAfter, expected)) / normOf(expected) < kExactOnAFixture);
        }
    }
}

TEST_CASE("CfdGradient_ATranslationAloneLeavesTheGradientUnchanged", "[cfd][grad]") {
    // THE DEGENERATE ARM OF COVARIANCE, which is a different claim: a pure
    // translation has `R = I`, so the gradient must come back UNCHANGED rather
    // than rotated. It is the arm that catches a transform applied to the
    // gradient where none was wanted.
    //
    // The translation is large compared with the cell, so subtractive
    // cancellation in the coordinate differences is the real risk here -- the
    // reason the structural suite tests a kilometre-scale shift separately.
    const Hand original = Hand::make(analyticPoints(), kAnalyticPair);
    const RigidTransform3D shift =
        RigidTransform3D::translation(Translation3D{1000_mm, -2500_mm, 700_mm});
    const meshing::Mesh movedMesh = transformed(original.mesh, shift);
    Result<CfdTopology> movedTopology = cfd::buildCfdTopology(movedMesh, onePatch(movedMesh));
    REQUIRE(movedTopology.has_value());
    Result<CfdGeometry> movedGeometry = cfd::buildCfdGeometry(movedMesh, *movedTopology);
    REQUIRE(movedGeometry.has_value());
    const Hand moved{movedMesh, std::move(*movedTopology), std::move(*movedGeometry)};
    REQUIRE(shift.isTranslation());

    const Vec gradient{7.2, -3.5, 11.4};
    const std::vector<double> values = linearCellValues(original.geometry, gradient, 0.0);
    std::vector<double> facetValues;
    for (std::size_t b = 0; b < original.topology.boundaryFaces(); ++b) {
        const std::optional<Point3D> centroid =
            original.geometry.faceCentroid(original.topology.faceOf(BoundaryFaceIndex::fromValue(b)));
        REQUIRE(centroid.has_value());
        facetValues.push_back(dotOf(gradient, siOf(*centroid)));
    }

    const PreparedCfdBoundary before = prescribedPressureBoundary(original, facetValues);
    const PreparedCfdBoundary after = prescribedPressureBoundary(moved, facetValues);
    const Result<CfdPressureGradientField> was = cfd::reconstructCfdPressureGradient(
        original.topology, original.geometry, before,
        pressureInput(values, CfdGradientMethod::LeastSquares));
    const Result<CfdPressureGradientField> is = cfd::reconstructCfdPressureGradient(
        moved.topology, moved.geometry, after, pressureInput(values, CfdGradientMethod::LeastSquares));
    REQUIRE(was.has_value());
    REQUIRE(is.has_value());

    double worst = 0.0;
    for (std::size_t c = 0; c < was->cells(); ++c) {
        const std::optional<CfdPressureGradientField::Vector> a = was->at(CellIndex::fromValue(c));
        const std::optional<CfdPressureGradientField::Vector> b = is->at(CellIndex::fromValue(c));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        const Vec one{a->x.si(), a->y.si(), a->z.si()};
        const Vec two{b->x.si(), b->y.si(), b->z.si()};
        worst = std::max(worst, normOf(subOf(one, two)) / normOf(gradient));
        // And both are the exact gradient, so the invariance is not two equal
        // wrong answers.
        CHECK(normOf(subOf(one, gradient)) / normOf(gradient) < kExactOnAFixture);
    }
    INFO("worst relative difference under a 2.7 m translation: " << worst);
    CHECK(worst < kExactOnAFixture);
}

TEST_CASE("CfdGradient_TheVelocityGradientTensorIsCovariantUnderARigidTransform", "[cfd][grad]") {
    // THE SUBTLETY THAT A PER-COMPONENT OPERATOR MAKES EASY TO GET WRONG, and it
    // is the reason this case exists separately from the pressure one.
    //
    // The gradient of ONE velocity component is one ROW of the tensor
    // `G_ij = du_i/dx_j`. Under a rotation the component itself turns, so the row
    // does NOT simply rotate: the whole tensor transforms as
    //
    //     G' = R G R^T
    //
    // and requiring `g'_i = R g_i` row by row would be WRONG -- the same mistake
    // the structural suite records for an element stiffness matrix, where
    // `Ke_rot = T Ke T^T` and not `Ke_rot = Ke`. So all three rows are
    // reconstructed and the tensor law is asserted.
    //
    // The transported field is `u'(x') = R u(motion^-1(x'))`, so each cell's
    // velocity VECTOR is rotated while its cell index stays put.
    const Hand original = Hand::make(analyticPoints(), kAnalyticPair);
    const RigidTransform3D motion = covarianceMotion();
    const meshing::Mesh movedMesh = transformed(original.mesh, motion);
    Result<CfdTopology> movedTopology = cfd::buildCfdTopology(movedMesh, onePatch(movedMesh));
    REQUIRE(movedTopology.has_value());
    Result<CfdGeometry> movedGeometry = cfd::buildCfdGeometry(movedMesh, *movedTopology);
    REQUIRE(movedGeometry.has_value());
    const Hand moved{movedMesh, std::move(*movedTopology), std::move(*movedGeometry)};

    const Mat rotation = motion.matrix();
    // A PROPER ROTATION, asserted: a reflection would turn these cells inside
    // out, and the claim is about rotations.
    CHECK_FALSE(motion.reversesOrientation());

    const Mat tensor{0.7, -1.3, 2.1, 5.5, 0.9, -3.3, -2.2, 4.4, 1.1};
    const std::vector<double> before = linearVelocity(original.geometry, tensor, Vec{0.0, 0.0, 0.0});

    // The moved field: the same physical velocity, rotated, at the same cells.
    const std::size_t cells = original.topology.cells();
    std::vector<double> after(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        const Vec was{before[0 * cells + c], before[1 * cells + c], before[2 * cells + c]};
        const Vec is = applyTo(rotation, was);
        for (std::size_t i = 0; i < 3; ++i) {
            after[i * cells + c] = is[i];
        }
    }

    // A no-slip wall everywhere: the velocity policy is FixedValue at zero on
    // both meshes, which is the SAME physical condition before and after -- a
    // zero vector is the one velocity a rotation leaves alone, so the boundary
    // cannot smuggle in a frame dependence.
    const PreparedCfdBoundary wallBefore = original.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const PreparedCfdBoundary wallAfter =
        moved.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    for (const CfdGradientMethod method : cfd::kCfdGradientMethods) {
        INFO(cfd::toString(method));
        double worst = 0.0;
        for (std::size_t c = 0; c < cells; ++c) {
            INFO("cell " << c);
            const CellIndex cell = CellIndex::fromValue(c);
            const Mat was = velocityTensor(original.topology, original.geometry, wallBefore, before,
                                           method, cell);
            const Mat is =
                velocityTensor(moved.topology, moved.geometry, wallAfter, after, method, cell);
            // R G R^T, with the oracle's own multiplication.
            const Mat expected = multiplyOf(multiplyOf(rotation, was), transposeOf(rotation));
            const double scale = std::max(worstEntryOf(was), 1.0e-300);
            for (std::size_t k = 0; k < 9; ++k) {
                worst = std::max(worst, std::abs(is[k] - expected[k]) / scale);
            }
            // THE NON-TRIVIALITY GUARD: the tensor must actually change under
            // the rotation, or `G' = R G R^T` is being checked against `G' = G`.
            double moved9 = 0.0;
            for (std::size_t k = 0; k < 9; ++k) {
                moved9 = std::max(moved9, std::abs(is[k] - was[k]) / scale);
            }
            INFO("the tensor moved by " << moved9 << " relative");
            CHECK(moved9 > 0.1);
        }
        INFO("worst relative deviation from R G R^T: " << worst);
        CHECK(worst < kExactOnAFixture);
    }
}

// ===========================================================================
// THE BOUNDARY CONTRIBUTION
// ===========================================================================

TEST_CASE("CfdGradient_AZeroGradientRowUsesTheNormalAndNotTheCellToFaceVector", "[cfd][grad]") {
    // ADR-070 DECISION 5, PINNED WITH AN INDEPENDENT ORACLE ON EACH SIDE.
    //
    // `ZeroGradient` means `g . n = 0`. Written as a least-squares row that is
    // `(h n, 0)` it says exactly that; written as `(C_f - C_P, 0)` it says
    // `g . (C_f - C_P) = 0`, which is the same statement ONLY where the
    // cell-to-face vector is parallel to the normal -- that is, only on an
    // orthogonal mesh. So the two forms agree exactly on the orthogonal pair and
    // the fixture here must be the ANALYTIC pair, where `cos(theta) = 3/5`.
    //
    // Both forms are built here, in the test's own arithmetic, and the operator
    // must match the first and differ from the second. Asserting only the first
    // would pass an operator using either.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    // A no-slip wall: the PRESSURE policy is ZeroGradient at every facet, which
    // is the column this case needs.
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::NoSlipWall).pressure ==
            CfdBoundaryPolicy::ZeroGradient);

    // A field with a real difference across the internal face, so the fit has
    // something to work with besides the boundary constraints.
    const std::vector<double> values{11.0, 29.0};
    REQUIRE(values.size() == hand.topology.cells());

    // THE TWO ORACLES. A 3x3 symmetric normal system, solved by cofactors, built
    // from the two candidate boundary rows -- written out here rather than read
    // from production.
    const auto fitWith = [&](bool useNormal, std::size_t cellIndex) {
        std::array<double, 6> m{};
        Vec rhs{};
        const auto row = [&](const Vec& d, double value) {
            const double w = 1.0 / dotOf(d, d);
            m[0] += w * d[0] * d[0];
            m[1] += w * d[0] * d[1];
            m[2] += w * d[0] * d[2];
            m[3] += w * d[1] * d[1];
            m[4] += w * d[1] * d[2];
            m[5] += w * d[2] * d[2];
            for (std::size_t i = 0; i < 3; ++i) {
                rhs[i] += w * d[i] * value;
            }
        };
        const CellIndex cell = CellIndex::fromValue(cellIndex);
        for (const FaceIndex face : hand.topology.cellFaces(cell)) {
            const CellIndex owner = hand.topology.owner(face);
            const bool isOwner = owner.value() == cellIndex;
            if (hand.topology.isInternal(face)) {
                const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
                const std::optional<Translation3D> span = hand.geometry.ownerToNeighbour(face);
                REQUIRE(neighbour.has_value());
                REQUIRE(span.has_value());
                const Vec stored = siOf(*span);
                const Vec d = isOwner ? stored : Vec{-stored[0], -stored[1], -stored[2]};
                const std::size_t other = isOwner ? neighbour->value() : owner.value();
                row(d, values[other] - values[cellIndex]);
                continue;
            }
            const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
            REQUIRE(normal.has_value());
            if (useNormal) {
                const double h = hand.geometry.orthogonalDistance(face).si();
                row(Vec{normal->x() * h, normal->y() * h, normal->z() * h}, 0.0);
            } else {
                row(siOf(hand.geometry.cellToFace(face)), 0.0);
            }
        }
        // Cofactor solve of the symmetric system.
        const double a = m[0];
        const double b = m[1];
        const double c = m[2];
        const double d = m[3];
        const double e = m[4];
        const double f = m[5];
        const double c00 = d * f - e * e;
        const double c01 = c * e - b * f;
        const double c02 = b * e - c * d;
        const double det = a * c00 + b * c01 + c * c02;
        REQUIRE(det > 0.0);
        const double c11 = a * f - c * c;
        const double c12 = b * c - a * e;
        const double c22 = a * d - b * b;
        return Vec{(c00 * rhs[0] + c01 * rhs[1] + c02 * rhs[2]) / det,
                   (c01 * rhs[0] + c11 * rhs[1] + c12 * rhs[2]) / det,
                   (c02 * rhs[0] + c12 * rhs[1] + c22 * rhs[2]) / det};
    };

    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::LeastSquares));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    double worstAgainstNormal = 0.0;
    double smallestAgainstCellToFace = std::numeric_limits<double>::infinity();
    for (std::size_t c = 0; c < field->cells(); ++c) {
        INFO("cell " << c);
        const std::optional<CfdPressureGradientField::Vector> g = field->at(CellIndex::fromValue(c));
        REQUIRE(g.has_value());
        const Vec got{g->x.si(), g->y.si(), g->z.si()};
        const Vec withNormal = fitWith(true, c);
        const Vec withCellToFace = fitWith(false, c);
        const double scale = std::max(normOf(withNormal), 1.0e-300);
        worstAgainstNormal = std::max(worstAgainstNormal, normOf(subOf(got, withNormal)) / scale);
        smallestAgainstCellToFace =
            std::min(smallestAgainstCellToFace, normOf(subOf(got, withCellToFace)) / scale);
        INFO("normal-form oracle " << withNormal[0] << " " << withNormal[1] << " " << withNormal[2]
                                   << ", cell-to-face-form oracle " << withCellToFace[0] << " "
                                   << withCellToFace[1] << " " << withCellToFace[2]);
    }
    INFO("agreement with the normal form " << worstAgainstNormal << ", distance from the cell-to-face form "
                                           << smallestAgainstCellToFace);
    CHECK(worstAgainstNormal < kExactOnAFixture);
    // AND THE TWO FORMS MUST ACTUALLY DIFFER on this fixture, or the test cannot
    // discriminate and the assertion above is satisfied by both.
    CHECK(smallestAgainstCellToFace > 1.0e-3);
}

TEST_CASE("CfdGradient_ASlipWallProjectsOutTheNormalComponentAndCouplesTheComponents", "[cfd][grad]") {
    // ADR-070 DECISION 4's FINDING, pinned. A slip wall gives
    // `U_b = U_P - (U_P . n) n`, so the boundary value of component `i` depends
    // on ALL THREE components of the owner cell's velocity. The gradient of one
    // velocity component is therefore NOT a function of that component's field
    // alone -- which is why the velocity entry point takes the whole
    // component-blocked array, and why no refusal for a missing velocity exists.
    //
    // The test changes ONLY the y and z components and requires the x
    // component's gradient to move. An operator that read one component's slice
    // could not do that, and would pass every other case in this file.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::Slip).velocity == CfdBoundaryPolicy::Symmetry);

    const std::size_t cells = hand.topology.cells();
    REQUIRE(cells == 1);

    // EVERY COMPONENT, NOT ONLY X. The mutation sweep found that a version of
    // this case asking only for component X could not see the normal's component
    // index hard-coded to `0`, because for component X that mutation IS the
    // original. Each of the three is exercised now, and the one being
    // differentiated is held FIXED while the other two change, so any movement
    // in the answer is the coupling and nothing else.
    const auto gradientOf = [&](VectorComponent component, const Vec& velocity) {
        std::vector<double> values(3 * cells, 0.0);
        for (std::size_t i = 0; i < 3; ++i) {
            values[i * cells + 0] = velocity[i];
        }
        const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
            hand.topology, hand.geometry, boundary,
            velocityInput(values, component, CfdGradientMethod::LeastSquares));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        CHECK(field->summary().symmetryFaces == hand.topology.boundaryFaces());
        const std::optional<CfdVelocityGradientField::Vector> g = field->at(CellIndex::fromValue(0));
        REQUIRE(g.has_value());
        return Vec{g->x.si(), g->y.si(), g->z.si()};
    };

    for (std::size_t which = 0; which < 3; ++which) {
        INFO("component " << which);
        Vec before{0.0, 0.0, 0.0};
        Vec after{0.0, 0.0, 0.0};
        before[which] = 3.0;
        after[which] = 3.0;
        for (std::size_t other = 0; other < 3; ++other) {
            if (other != which) {
                after[other] = other == (which + 1) % 3 ? 7.0 : -5.0;
            }
        }
        const Vec one = gradientOf(cfd::kVectorComponents[which], before);
        const Vec two = gradientOf(cfd::kVectorComponents[which], after);
        INFO("grad u_" << which << " before " << one[0] << " " << one[1] << " " << one[2]);
        INFO("grad u_" << which << " after  " << two[0] << " " << two[1] << " " << two[2]);
        CHECK(normOf(subOf(one, two)) > 1.0);
    }

    // A SECOND LOOP WAS HERE AND IS GONE. It asserted that
    // `slipOracle(v, n) . n == 0` at every facet -- which is a property of this
    // file's own two-line helper, identically true for any unit `n`, with no
    // production value anywhere in it. A test-gap review called it testing the
    // test, and it was right.
    //
    // The projection IS pinned against the operator, by
    // `CfdGradient_ASlipWallGradientMatchesAFitBuiltFromTheProjectionOracle`,
    // which builds the whole least-squares fit from `slipOracle` and requires
    // the operator to land on it.
    CHECK(hand.topology.boundaryFaces() == 4);
}

TEST_CASE("CfdGradient_AMixedBoundaryIsCountedPolicyByPolicyAndTheCensusIsComplete", "[cfd][grad]") {
    // THE CENSUS, AND THE CLAIM THAT IT IS COMPLETE. ADR-067's table has no
    // fourth value down the `p` or `U` columns and `Computed` is refused, so the
    // three counts must sum to the boundary face count -- which is a stronger
    // statement than each count being plausible, and it is what catches a
    // counter incremented in the wrong switch arm.
    //
    // A MIXED BOUNDARY IS REQUIRED to make that observable: with one condition
    // everywhere, two arms are zero and a swap between them cannot show.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const ElementId onZ = facetWhere(hand, [](const Point3D& p) { return nearZeroLength(p.z); });
    const ElementId onY = facetWhere(hand, [](const Point3D& p) { return nearZeroLength(p.y); });
    const ElementId onX = facetWhere(hand, [](const Point3D& p) { return nearZeroLength(p.x); });
    REQUIRE(onZ.value() != onY.value());
    REQUIRE(onY.value() != onX.value());
    REQUIRE(onZ.value() != onX.value());

    std::vector<FacetCondition> assignment;
    // A slip wall: velocity Symmetry, pressure ZeroGradient.
    assignment.push_back(FacetCondition{
        onZ, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace(1)}}});
    // A no-slip wall: velocity FixedValue, pressure ZeroGradient.
    assignment.push_back(FacetCondition{
        onY, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), NoSlipWall{someFace(2)}}});
    // A pressure outlet: velocity ZeroGradient, pressure FixedValue.
    assignment.push_back(FacetCondition{
        onX, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3),
                                  PressureOutlet{someFace(3), 0_Pa}}});
    // The slanted facet, another no-slip wall.
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const ElementId facet = hand.topology.triangleOf(BoundaryFaceIndex::fromValue(b));
        if (facet.value() != onZ.value() && facet.value() != onY.value() &&
            facet.value() != onX.value()) {
            assignment.push_back(FacetCondition{
                facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(4),
                                            NoSlipWall{someFace(4)}}});
        }
    }
    REQUIRE(assignment.size() == hand.topology.boundaryFaces());
    // One facet fixes the pressure, so ADR-067 Decision 7 refuses a datum.
    const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, false);

    const std::vector<double> pressures(hand.topology.cells(), 17.0);
    const Result<CfdPressureGradientField> pressure = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(pressures, CfdGradientMethod::LeastSquares));
    INFO((pressure.has_value() ? std::string{} : pressure.error().message));
    REQUIRE(pressure.has_value());
    // Down the PRESSURE column: three ZeroGradient and one FixedValue, and the
    // pressure column holds no Symmetry at all.
    CHECK(pressure->summary().fixedValueFaces == 1);
    CHECK(pressure->summary().zeroGradientFaces == 3);
    CHECK(pressure->summary().symmetryFaces == 0);

    const std::vector<double> velocities(3 * hand.topology.cells(), 0.0);
    const Result<CfdVelocityGradientField> velocity = cfd::reconstructCfdVelocityGradient(
        hand.topology, hand.geometry, boundary,
        velocityInput(velocities, VectorComponent::X, CfdGradientMethod::LeastSquares));
    INFO((velocity.has_value() ? std::string{} : velocity.error().message));
    REQUIRE(velocity.has_value());
    // Down the VELOCITY column: two FixedValue walls, one ZeroGradient outlet,
    // one Symmetry slip wall. A DIFFERENT split from the pressure's, which is
    // what makes the two columns distinguishable.
    CHECK(velocity->summary().fixedValueFaces == 2);
    CHECK(velocity->summary().zeroGradientFaces == 1);
    CHECK(velocity->summary().symmetryFaces == 1);

    // THE COMPLETENESS CLAIM, for both columns.
    for (const CfdGradientSummary& summary : {pressure->summary(), velocity->summary()}) {
        CHECK(summary.fixedValueFaces + summary.zeroGradientFaces + summary.symmetryFaces ==
              hand.topology.boundaryFaces());
    }
}

TEST_CASE("CfdGradient_ADirichletFacetMakesGreenGaussExactAtThatFace", "[cfd][grad]") {
    // ADR-070 DECISION 4's WARNING, MADE MEASURABLE: a `FixedValue` facet gives
    // the exact value AT the face centroid, so Green-Gauss pays no interpolation
    // error there. The consequence is that a Dirichlet-dominated mesh FLATTERS
    // Green-Gauss, and a suite that measured the skew penalty only on such a
    // mesh would understate it.
    //
    // The one-cell tet has four boundary faces and NO internal face, so with
    // every facet prescribing the linear field's own value, Green-Gauss has
    // nothing to interpolate and must be exact -- on a fixture whose
    // non-orthogonality is real.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.internalFaces() == 0);
    const Vec gradient{7.2, -3.5, 11.4};
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 0.0);

    const Reading gauss = readPressureGradient(hand.topology, hand.geometry, boundary, gradient, 0.0,
                                               CfdGradientMethod::GreenGauss);
    INFO("Green-Gauss relative error with every facet Dirichlet: " << gauss.worstError);
    CHECK(gauss.worstError < kExactOnAFixture);
    // And it reports zero skewness, because it interpolated nothing -- not
    // because the mesh is unskewed.
    CHECK(gauss.summary.worstSkewness == 0.0);
    CHECK(gauss.summary.fixedValueFaces == 4);
}

// `CfdGradient_TheInterpolationWeightMatchesAnIndependentNormalProjection` WAS
// HERE AND IS DELETED. A test-gap review found that it never called the operator
// at all: every assertion was arithmetic on this file's own `weightOracle`, and
// one of them -- `CHECK(value + weight * (value - value) == value)` -- reduces to
// `value + weight * 0.0 == value`, which is true for every finite weight and
// every value. Its own comment claimed "the oracle's weight is used to predict
// Green-Gauss's answer on the orthogonal pair", and no such prediction was in
// the body. It could not fail for any mutation of the operator.
//
// The weight IS properly pinned, against both candidate denominators with the
// discrimination asserted, by
// `CfdGradient_GreenGaussMatchesASurfaceSumBuiltWithTheNormalProjectionWeight`.
// Deleting a test that reads as evidence and is not is worth more than keeping
// it: P19-IC-001's review found the same thing in a block that called itself
// "THE DISCRIMINATING PART".

// ===========================================================================
// SI UNITS
// ===========================================================================

TEST_CASE("CfdGradient_TheGradientQuantitiesCarryTheDimensionsTheirNamesClaim", "[cfd][grad]") {
    // THE UNITS CLAIM, CHECKED AT COMPILE TIME AND THEN ASSERTED. ADR-070
    // Decision 3 adds no `core/units` alias: `Quantity` is templated on a
    // `Dimension` with compile-time division, so `decltype(Pressure{}/Length{})`
    // already IS the right type. These static assertions are what make that a
    // guarantee rather than a hope, and the runtime checks make the dimensions
    // visible in a failure message.
    static_assert(std::is_same_v<PressureGradient, decltype(Pressure{} / Length{})>);
    static_assert(std::is_same_v<VelocityGradient, decltype(Velocity{} / Length{})>);
    static_assert(PressureGradient::dimension == dimensions::pressure / dimensions::length);
    static_assert(VelocityGradient::dimension == dimensions::velocity / dimensions::length);
    // AND THE TWO ARE DIFFERENT TYPES, which is what stops a pressure gradient
    // being passed where a velocity gradient is wanted. The compile-fail group
    // holds the gates that prove the compiler refuses it; this is the statement
    // of why.
    static_assert(!std::is_same_v<PressureGradient, VelocityGradient>);
    static_assert(!std::is_same_v<CfdPressureGradientField, CfdVelocityGradientField>);

    CHECK(PressureGradient::dimension == dimensions::pressure / dimensions::length);
    CHECK(VelocityGradient::dimension == dimensions::velocity / dimensions::length);
    // A velocity gradient is a reciprocal time, which is the one sanity check a
    // reader can do in their head.
    CHECK(VelocityGradient::dimension == dimensions::time.inverse());

    // And a reconstructed value really is in those units, not a bare double
    // wearing a name: it has to be built through `fromSi` and read through
    // `si()`, and the round trip is exact.
    CHECK(PressureGradient::fromSi(2.5).si() == 2.5);
    CHECK(VelocityGradient::fromSi(-7.25).si() == -7.25);
}

TEST_CASE("CfdGradient_AReconstructedFieldIsTypedByItsQuantity", "[cfd][grad]") {
    // The field's own typedefs, so a consumer writing `auto` gets the right
    // thing and a consumer writing the type out gets a compiler error if it
    // changes.
    static_assert(std::is_same_v<CfdPressureGradientField::Component, PressureGradient>);
    static_assert(std::is_same_v<CfdVelocityGradientField::Component, VelocityGradient>);
    static_assert(std::is_same_v<CfdPressureGradientField::Vector, cfd::PressureGradient3>);
    static_assert(std::is_same_v<CfdVelocityGradientField::Vector, cfd::VelocityGradient3>);

    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 500_Pa}});
    const std::vector<double> values(hand.topology.cells(), 0.0);
    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::LeastSquares));
    REQUIRE(field.has_value());

    // `largestMagnitude` returns the quantity, not a double.
    const PressureGradient largest = field->largestMagnitude();
    static_assert(std::is_same_v<decltype(largest), const PressureGradient>);
    CHECK(largest.si() > 0.0);

    // And a cell outside the field is an empty optional, not undefined
    // behaviour -- total, like every other accessor in this module.
    CHECK_FALSE(field->at(CellIndex::fromValue(hand.topology.cells())).has_value());
    CHECK_FALSE(field->at(CellIndex{}).has_value());
    CHECK(field->at(CellIndex::fromValue(0)).has_value());
}

// ===========================================================================
// DETERMINISM
// ===========================================================================

TEST_CASE("CfdGradient_TwoReconstructionsOfTheSameInputAreBitIdentical", "[cfd][grad]") {
    // ADR-070 DECISION 9. The accumulation order is `cellFaces`', which is
    // stored, and no unordered container reaches the result -- so two
    // reconstructions must agree to the last bit, not merely closely. Value
    // equality on the field is the check, and it compares the stored SI values.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const Vec gradient{7.2, -3.5, 11.4};
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 305.7);
    const std::vector<double> values = linearCellValues(hand.geometry, gradient, 305.7);

    for (const CfdGradientMethod method : cfd::kCfdGradientMethods) {
        INFO(cfd::toString(method));
        const Result<CfdPressureGradientField> first = cfd::reconstructCfdPressureGradient(
            hand.topology, hand.geometry, boundary, pressureInput(values, method));
        const Result<CfdPressureGradientField> second = cfd::reconstructCfdPressureGradient(
            hand.topology, hand.geometry, boundary, pressureInput(values, method));
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(*first == *second);
        CHECK(first->summary() == second->summary());
        for (std::size_t c = 0; c < first->cells(); ++c) {
            const std::optional<CfdPressureGradientField::Vector> a =
                first->at(CellIndex::fromValue(c));
            const std::optional<CfdPressureGradientField::Vector> b =
                second->at(CellIndex::fromValue(c));
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            CHECK(a->x.si() == b->x.si());
            CHECK(a->y.si() == b->y.si());
            CHECK(a->z.si() == b->z.si());
        }
    }
}

TEST_CASE("CfdGradient_TheFieldEqualityIsValueEqualityAndNotIdentity", "[cfd][grad]") {
    // THE OTHER HALF OF THE CLAIM ABOVE, and the one that stops it being a
    // tautology: if `operator==` were always true, the bit-identity test would
    // pass whatever the operator did. A DIFFERENT input must compare unequal.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const Vec gradient{7.2, -3.5, 11.4};
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 0.0);
    const std::vector<double> values = linearCellValues(hand.geometry, gradient, 0.0);
    std::vector<double> nudged = values;
    nudged[0] += 1.0;

    const Result<CfdPressureGradientField> plain = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary,
        pressureInput(values, CfdGradientMethod::LeastSquares));
    const Result<CfdPressureGradientField> changed = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary,
        pressureInput(nudged, CfdGradientMethod::LeastSquares));
    REQUIRE(plain.has_value());
    REQUIRE(changed.has_value());
    CHECK_FALSE(*plain == *changed);

    // And the two METHODS disagree on this skewed fixture, so the summary's
    // method field is not the only thing that distinguishes them.
    const Result<CfdPressureGradientField> gauss = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::GreenGauss));
    REQUIRE(gauss.has_value());
    CHECK_FALSE(*plain == *gauss);
}

TEST_CASE("CfdGradient_EveryReferenceBodyReconstructsBitIdenticallyTwice", "[cfd][grad]") {
    // Determinism AT SCALE, where an unordered iteration would actually have
    // room to show: two cells cannot distinguish an order, and these bodies
    // reach hundreds.
    std::size_t bodies = 0;
    std::size_t cells = 0;
    const Vec gradient{7.2, -3.5, 11.4};
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        const PreparedCfdBoundary boundary = body.wallEverywhere();
        const std::vector<double> values = linearCellValues(*body.geometry, gradient, 0.0);
        const Result<CfdPressureGradientField> first = cfd::reconstructCfdPressureGradient(
            *body.topology, *body.geometry, boundary,
            pressureInput(values, CfdGradientMethod::LeastSquares));
        const Result<CfdPressureGradientField> second = cfd::reconstructCfdPressureGradient(
            *body.topology, *body.geometry, boundary,
            pressureInput(values, CfdGradientMethod::LeastSquares));
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(*first == *second);
        CHECK(first->cells() == body.topology->cells());
        cells += first->cells();
    }
    INFO("bodies " << bodies << ", cells " << cells);
    CHECK(bodies == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(bodies == 8);
    // THE CELL COUNT IS A NON-TRIVIALITY GUARD: a sweep over eight empty meshes
    // would satisfy every assertion above.
    CHECK(cells > 500);
}

// ===========================================================================
// THE CONDITIONING REPORT
// ===========================================================================

TEST_CASE("CfdGradient_TheWorstConditioningIsReportedAndEveryReferenceBodyIsWithinTheLimit",
          "[cfd][grad]") {
    // ADR-070 DECISION 7. The conditioning is REPORTED, not acted on: an
    // engineering threshold on mesh quality is P19-VALID-001's and the owner's,
    // by ADR-063 finding F1, and the only thing refused here is a system that is
    // numerically rank-deficient.
    //
    // THE USEFUL CLAIM IS THAT THE LIMIT REFUSES NOTHING REAL. If a reference
    // body were near the limit, the limit would be making a mesh-acceptance
    // decision by the back door.
    std::size_t bodies = 0;
    double worst = 0.0;
    const Vec gradient{7.2, -3.5, 11.4};
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        const PreparedCfdBoundary boundary = body.wallEverywhere();
        const std::vector<double> values = linearCellValues(*body.geometry, gradient, 0.0);
        const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
            *body.topology, *body.geometry, boundary,
            pressureInput(values, CfdGradientMethod::LeastSquares));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        const double reported = field->summary().worstConditioning;
        INFO("worst conditioning on this body: " << reported);
        // AT LEAST ONE for a solved symmetric positive-definite system, because
        // it is a ratio of a largest to a smallest positive eigenvalue. A zero
        // here would mean no system was solved.
        CHECK(reported >= 1.0);
        CHECK(reported < cfd::kCfdGradientConditioningLimit);
        // And comfortably inside it, not just inside it.
        CHECK(reported < 1.0e-3 * cfd::kCfdGradientConditioningLimit);
        worst = std::max(worst, reported);
    }
    INFO("bodies " << bodies << ", worst conditioning over all of them: " << worst);
    CHECK(bodies == 8);
    // A NON-TRIVIALITY GUARD on the other side: if every body came back at
    // exactly 1.0 the measurement would not be reading the mesh at all.
    CHECK(worst > 1.0);
}

TEST_CASE("CfdGradient_GreenGaussReportsZeroConditioningBecauseItSolvesNothing", "[cfd][grad]") {
    // Each method fills its own summary field and leaves the other's at zero, so
    // a zero is a statement ABOUT THE METHOD and not a missing measurement. This
    // pins both directions at once, which is what catches the two being swapped.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const Vec gradient{7.2, -3.5, 11.4};
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 0.0);
    const std::vector<double> values = linearCellValues(hand.geometry, gradient, 0.0);

    const Result<CfdPressureGradientField> gauss = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::GreenGauss));
    const Result<CfdPressureGradientField> squares = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::LeastSquares));
    REQUIRE(gauss.has_value());
    REQUIRE(squares.has_value());

    CHECK(gauss->summary().worstConditioning == 0.0);
    CHECK(gauss->summary().worstSkewness > 0.0);
    CHECK(squares->summary().worstConditioning >= 1.0);
    CHECK(squares->summary().worstSkewness == 0.0);
    CHECK(gauss->summary().method == CfdGradientMethod::GreenGauss);
    CHECK(squares->summary().method == CfdGradientMethod::LeastSquares);
    CHECK(cfd::toString(CfdGradientMethod::GreenGauss) == "green_gauss");
    CHECK(cfd::toString(CfdGradientMethod::LeastSquares) == "least_squares");
}

// ===========================================================================
// THE REFUSALS
// ===========================================================================

TEST_CASE("CfdGradient_EveryProblemHasItsOwnName", "[cfd][grad]") {
    // The names are what a caller that owns the vocabulary reports in its own
    // words, so two problems sharing a name would be a silent conflation.
    std::vector<std::string_view> names;
    for (const CfdGradientProblem problem : cfd::kCfdGradientProblems) {
        const std::string_view name = cfd::toString(problem);
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
        names.push_back(name);
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    // THE COUNT IS A LITERAL, AND ITS WEAKNESS IS STATED. Comparing against
    // `kCfdGradientProblems.size()` would be a tautology, since the loop
    // iterates that array; the literal below is what a reader checks against
    // the header's own static_assert, which is the real guard. A test-gap
    // review pointed out that calling this figure "independent" overstates it
    // -- it is in a different file, and that is all.
    //
    // NINE, after two independent reviews added two: `SymmetryNeedsAVectorField`
    // for an out-of-bounds read, and `GradientIsNotFinite` for a NaN that was
    // being returned as a success.
    CHECK(names.size() == 9);
    CHECK(cfd::kCfdGradientProblems.size() == 9);
}

TEST_CASE("CfdGradient_AGeometryBuiltForAnotherMeshIsRefusedFirst", "[cfd][grad]") {
    // PHASE 1, and first for a reason: every later phase reads both the topology
    // and the metrics, so a mismatch has to be caught before anything indexes
    // through it.
    const Hand one = Hand::make(analyticPoints(), kAnalyticPair);
    const Hand two = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = one.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});
    const std::vector<double> values(one.topology.cells(), 0.0);

    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        one.topology, two.geometry, boundary,
        pressureInput(values, CfdGradientMethod::LeastSquares));
    REQUIRE_FALSE(field.has_value());
    CHECK(field.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(field.error().message, ContainsSubstring("different mesh generation"));
    // AND THE TAG, which is what a caller switches on. Asserted by name against
    // an independently written expectation, not against whatever the operator
    // returned -- the lesson P19-BC-001's review recorded.
    CHECK(cfd::cfdPressureGradientProblem(one.topology, two.geometry, boundary,
                                          pressureInput(values, CfdGradientMethod::LeastSquares)) ==
          CfdGradientProblem::GeometryDescribesAnotherTopology);
}

TEST_CASE("CfdGradient_ABoundaryCoveringAnotherFaceCountIsRefused", "[cfd][grad]") {
    // PHASE 2, and STRUCTURAL ONLY -- by face count, not by stamp.
    // `bindCfdBoundary` deliberately returns a default-constructed source, so
    // comparing a stamp unconditionally would refuse every seam-built boundary
    // and make both analytic fixtures unreachable. That is the defect
    // P19-DIFF-001 found in its own gate before any test ran, and this operator
    // inherited the fix rather than the defect.
    const Hand pair = Hand::make(analyticPoints(), kAnalyticPair);
    const Hand single = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(pair.topology.boundaryFaces() != single.topology.boundaryFaces());

    const PreparedCfdBoundary wrong = single.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});
    const std::vector<double> values(pair.topology.cells(), 0.0);

    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        pair.topology, pair.geometry, wrong, pressureInput(values, CfdGradientMethod::LeastSquares));
    REQUIRE_FALSE(field.has_value());
    CHECK_THAT(field.error().message, ContainsSubstring("boundary faces"));
    CHECK(cfd::cfdPressureGradientProblem(pair.topology, pair.geometry, wrong,
                                          pressureInput(values, CfdGradientMethod::LeastSquares)) ==
          CfdGradientProblem::BoundaryDescribesAnotherTopology);
}

TEST_CASE("CfdGradient_AWrongCellValueLengthIsRefusedDownBothColumns", "[cfd][grad]") {
    // PHASE 3, and the two entry points expect DIFFERENT lengths: one value per
    // cell for a pressure, three for a component-blocked velocity. A single
    // length check would pass one of these wrongly.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const std::size_t cells = hand.topology.cells();

    SECTION("the pressure wants one value per cell") {
        for (const std::size_t wrong : {cells - 1, cells + 1, 3 * cells, std::size_t{0}}) {
            INFO("length " << wrong << " against " << cells);
            const std::vector<double> values(wrong, 1.0);
            const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
                hand.topology, hand.geometry, boundary,
                pressureInput(values, CfdGradientMethod::LeastSquares));
            REQUIRE_FALSE(field.has_value());
            CHECK_THAT(field.error().message, ContainsSubstring("cell-centred pressure"));
            CHECK(cfd::cfdPressureGradientProblem(
                      hand.topology, hand.geometry, boundary,
                      pressureInput(values, CfdGradientMethod::LeastSquares)) ==
                  CfdGradientProblem::CellValueLengthWrong);
        }
    }

    SECTION("the velocity wants three") {
        for (const std::size_t wrong : {cells, 3 * cells - 1, 3 * cells + 1, std::size_t{0}}) {
            INFO("length " << wrong << " against " << 3 * cells);
            const std::vector<double> values(wrong, 1.0);
            const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
                hand.topology, hand.geometry, boundary,
                velocityInput(values, VectorComponent::X, CfdGradientMethod::LeastSquares));
            REQUIRE_FALSE(field.has_value());
            CHECK_THAT(field.error().message, ContainsSubstring("component-blocked velocity"));
            CHECK(cfd::cfdVelocityGradientProblem(
                      hand.topology, hand.geometry, boundary,
                      velocityInput(values, VectorComponent::X, CfdGradientMethod::LeastSquares)) ==
                  CfdGradientProblem::CellValueLengthWrong);
        }
        // AND THE RIGHT LENGTH IS ACCEPTED, so the loop above is not refusing
        // everything for some other reason.
        const std::vector<double> right(3 * cells, 0.0);
        CHECK_FALSE(cfd::cfdVelocityGradientProblem(
                        hand.topology, hand.geometry, boundary,
                        velocityInput(right, VectorComponent::X, CfdGradientMethod::LeastSquares))
                        .has_value());
    }
}

TEST_CASE("CfdGradient_ANonFiniteCellValueIsRefusedAndTheLengthIsCheckedFirst", "[cfd][grad]") {
    // PHASE 4, and the ORDER is the point. A review of P19-DIFF-001 found both
    // of its caller-supplied arrays validated for LENGTH and never for VALUE, so
    // one NaN flowed through into the source while the matrix stayed provably
    // finite. The same two checks are here, and the length comes first so a
    // wrong length is reported as a length and not as whatever happens to sit in
    // it.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const std::size_t cells = hand.topology.cells();

    for (const double poison : {std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity()}) {
        for (std::size_t slot = 0; slot < cells; ++slot) {
            INFO("poison at " << slot);
            std::vector<double> values(cells, 1.0);
            values[slot] = poison;
            const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
                hand.topology, hand.geometry, boundary,
                pressureInput(values, CfdGradientMethod::LeastSquares));
            REQUIRE_FALSE(field.has_value());
            CHECK_THAT(field.error().message, ContainsSubstring("not finite"));
            CHECK(cfd::cfdPressureGradientProblem(
                      hand.topology, hand.geometry, boundary,
                      pressureInput(values, CfdGradientMethod::LeastSquares)) ==
                  CfdGradientProblem::CellValueNotFinite);
        }
        // EVERY COMPONENT OF THE VELOCITY, not just the first: the measured gap
        // P19-IC-001 closed was a NaN test that exercised one component and
        // would have passed unchanged checking only `x`.
        for (std::size_t slot = 0; slot < 3 * cells; ++slot) {
            INFO("velocity poison at " << slot);
            std::vector<double> values(3 * cells, 0.0);
            values[slot] = poison;
            const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
                hand.topology, hand.geometry, boundary,
                velocityInput(values, VectorComponent::Y, CfdGradientMethod::LeastSquares));
            REQUIRE_FALSE(field.has_value());
            CHECK(cfd::cfdVelocityGradientProblem(
                      hand.topology, hand.geometry, boundary,
                      velocityInput(values, VectorComponent::Y, CfdGradientMethod::LeastSquares)) ==
                  CfdGradientProblem::CellValueNotFinite);
        }
    }

    // THE PHASE ORDER: an input that is BOTH too short AND holds a NaN reports
    // the LENGTH, which is the earlier phase.
    std::vector<double> both(cells + 1, 1.0);
    both[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK(cfd::cfdPressureGradientProblem(hand.topology, hand.geometry, boundary,
                                          pressureInput(both, CfdGradientMethod::LeastSquares)) ==
          CfdGradientProblem::CellValueLengthWrong);
}

TEST_CASE("CfdGradient_ThePredictorAgreesWithTheReconstructionOnEveryReachableRefusal", "[cfd][grad]") {
    // RECORDED FOR WHAT IT IS WORTH, which is less than it looks. The predictor
    // and the reconstruction share ONE decision path, so their agreement cannot
    // fail -- exactly as P19-DIFF-001's evidence recorded of its own predictor
    // (its limitation L8). What this case actually pins is that the predictor
    // allocates nothing on the success path and that a successful input predicts
    // no problem, which a caller does rely on.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const Vec gradient{7.2, -3.5, 11.4};
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 0.0);
    const std::vector<double> values = linearCellValues(hand.geometry, gradient, 0.0);

    for (const CfdGradientMethod method : cfd::kCfdGradientMethods) {
        INFO(cfd::toString(method));
        const CfdGradientInput input = pressureInput(values, method);
        CHECK_FALSE(cfd::cfdPressureGradientProblem(hand.topology, hand.geometry, boundary, input)
                        .has_value());
        CHECK(cfd::reconstructCfdPressureGradient(hand.topology, hand.geometry, boundary, input)
                  .has_value());
    }

    const PreparedCfdBoundary wall =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const std::vector<double> velocities(3 * hand.topology.cells(), 0.0);
    for (const VectorComponent component : cfd::kVectorComponents) {
        for (const CfdGradientMethod method : cfd::kCfdGradientMethods) {
            const CfdVelocityGradientInput input = velocityInput(velocities, component, method);
            CHECK_FALSE(
                cfd::cfdVelocityGradientProblem(hand.topology, hand.geometry, wall, input).has_value());
            CHECK(cfd::reconstructCfdVelocityGradient(hand.topology, hand.geometry, wall, input)
                      .has_value());
        }
    }
}

TEST_CASE("CfdGradient_AFlatTetrahedronIsRefusedByTheGradientGateThoughTheMetricsAcceptIt",
          "[cfd][grad]") {
    // THE DEGENERATE ARM, asked honestly rather than claimed.
    //
    // `NeighbourhoodIsDegenerate` fires when the normal matrix is numerically
    // rank-deficient. A FLAT tetrahedron is the way to reach that: all four of
    // its faces are nearly parallel to one plane, so four `ZeroGradient` rows --
    // which are the face normals -- are nearly parallel too, and the fit has no
    // third direction. Whether such a cell can exist at all is `buildCfdGeometry`'s
    // decision, not this operator's.
    //
    // THE DISJUNCTION WAS ASKED FIRST AND THE ANSWER MEASURED, then pinned. It
    // is not what this author expected: `buildCfdGeometry` accepts EVERY one of
    // these, down to a thickness of 1e-15, because its refusals are a
    // non-positive cell volume and an inconsistent face orientation (ADR-064)
    // and a flat tetrahedron has neither. Four of the five are then refused HERE.
    //
    // So `NeighbourhoodIsDegenerate` is reachable, and the sharpest thing this
    // case says is that the gradient gate catches a mesh the metrics do not --
    // which is a reason for the gate to exist rather than a duplicate of a check
    // made upstream.
    const auto flatPoints = [](double thickness) {
        const auto p = [](double x, double y, double z) {
            return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
        };
        return std::vector<Point3D>{p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0.3, 0.3, thickness)};
    };

    std::size_t refusedByMetrics = 0;
    std::size_t refusedByTheGate = 0;
    std::size_t solved = 0;
    double worstConditioning = 0.0;

    for (const double thickness : {1.0e-3, 1.0e-6, 1.0e-9, 1.0e-12, 1.0e-15}) {
        INFO("thickness " << thickness);
        const meshing::Mesh mesh = buildMesh(flatPoints(thickness), kOneCell);
        const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
        if (!topology.has_value()) {
            ++refusedByMetrics;
            continue;
        }
        const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
        if (!geometry.has_value()) {
            INFO("the metrics refused it: " << geometry.error().message);
            ++refusedByMetrics;
            continue;
        }

        std::vector<ElementId> ascending;
        for (std::size_t b = 0; b < topology->boundaryFaces(); ++b) {
            ascending.push_back(topology->triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        const CfdResolvedTarget target{
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}},
            ascending};
        const std::array<CfdResolvedTarget, 1> targets{target};
        const std::optional<Point3D> centroid = geometry->cellCentroid(CellIndex::fromValue(0));
        REQUIRE(centroid.has_value());
        const cfd::CfdReferencePressure datum{.value = 0_Pa, .location = *centroid};
        const Result<PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            *topology, *geometry, mesh, targets, 0, datum, std::nullopt, std::nullopt);
        if (!bound.has_value()) {
            INFO("the binding refused it: " << bound.error().message);
            ++refusedByMetrics;
            continue;
        }

        const std::vector<double> values{11.0};
        const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
            *topology, *geometry, *bound, pressureInput(values, CfdGradientMethod::LeastSquares));
        if (!field.has_value()) {
            INFO("the gradient gate refused it: " << field.error().message);
            CHECK_THAT(field.error().message, ContainsSubstring("does not determine a gradient"));
            CHECK(cfd::cfdPressureGradientProblem(
                      *topology, *geometry, *bound,
                      pressureInput(values, CfdGradientMethod::LeastSquares)) ==
                  CfdGradientProblem::NeighbourhoodIsDegenerate);
            ++refusedByTheGate;
            continue;
        }
        // Solved: then it must be WITHIN the limit, and the answer must be
        // finite. A degenerate system that solved anyway would be the real
        // defect.
        const double reported = field->summary().worstConditioning;
        INFO("solved, conditioning " << reported);
        CHECK(reported >= 1.0);
        CHECK(reported <= cfd::kCfdGradientConditioningLimit);
        CHECK(std::isfinite(field->largestMagnitude().si()));
        worstConditioning = std::max(worstConditioning, reported);
        ++solved;
    }

    INFO("refused by the metrics " << refusedByMetrics << ", refused by the gradient gate "
                                   << refusedByTheGate << ", solved " << solved
                                   << ", worst conditioning among the solved " << worstConditioning);
    // EVERY THICKNESS LANDED SOMEWHERE, and nothing returned a non-finite
    // gradient.
    CHECK(refusedByMetrics + refusedByTheGate + solved == 5);
    // AND THE SPLIT ITSELF, measured before it was asserted: the metrics accept
    // all five, the gradient gate refuses the three from 1e-9 down, and the
    // 1e-2, 1e-3 and 1e-6 cells are solved -- the worst of them at about 3.6e11,
    // which is inside the limit by a factor of nearly three.
    //
    // THIS SPLIT WAS 4 AND 1 BEFORE THE EIGENVALUE RATIO WAS CORRECTED, and the
    // change is the clearest evidence that the correction was needed. The 1e-6
    // cell's true conditioning is 3.6e11, comfortably inside the 1e12 limit, and
    // the textbook trigonometric form computed a non-positive smallest
    // eigenvalue for it -- so it was being refused because the formula broke and
    // not because its neighbourhood failed to determine a gradient. An
    // independent Jacobi oracle found that; a transcribed oracle could not have.
    CHECK(refusedByMetrics == 0);
    CHECK(refusedByTheGate == 3);
    CHECK(solved == 2);
    CHECK(worstConditioning > 1.0e11);
    CHECK(worstConditioning < cfd::kCfdGradientConditioningLimit);
}

namespace {

/// The cells whose EVERY face is internal, on @p topology.
///
/// WHY THESE MATTER. An interior cell's least-squares fit takes no boundary row
/// at all, so its gradient depends only on the four neighbour differences -- and
/// for an exactly linear field those are exact whatever the boundary prescribes.
/// That is the only place the VELOCITY column's exactness can be asserted on
/// these meshes: P19-BC-001 requires a moving wall's velocity to be TANGENTIAL
/// to its facet, so an arbitrary linear velocity field cannot be prescribed
/// facet by facet the way a pressure can, and every two-cell fixture has every
/// cell on the boundary.
[[nodiscard]] std::vector<CellIndex> interiorCells(const CfdTopology& topology) {
    std::vector<CellIndex> out;
    for (std::size_t c = 0; c < topology.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        bool allInternal = true;
        for (const FaceIndex face : topology.cellFaces(cell)) {
            allInternal = allInternal && topology.isInternal(face);
        }
        if (allInternal) {
            out.push_back(cell);
        }
    }
    return out;
}

} // namespace

TEST_CASE("CfdGradient_BothColumnsAreExactOnAnInteriorCellWhateverTheBoundarySays", "[cfd][grad]") {
    // THE GAP THE AUTHOR'S OWN ADVERSARIAL PASS FOUND, and the one that mattered
    // most of the nine: the VELOCITY column's linear exactness was not asserted
    // anywhere. The three-rows-differ case only shows the component selector
    // works, and the covariance case compares two reconstructions of the same
    // answer -- it would pass if both were wrong in the same covariant way.
    //
    // An INTERIOR cell is where the claim can be made. Its fit takes only the
    // four neighbour differences, which are exact for a linear field, so the
    // boundary cannot interfere -- which is just as well, because a no-slip wall
    // prescribes zero velocity where the field is not zero and there is no
    // boundary condition in ADR-067's table that can prescribe an arbitrary
    // linear velocity facet by facet.
    //
    // The pressure arm is here too, with a DELIBERATELY INCONSISTENT boundary,
    // because that is the sharper statement: an interior cell is exact even when
    // every facet prescribes something the field does not satisfy.
    const Vec gradient{7.2, -3.5, 11.4};
    const Mat tensor{0.7, -1.3, 2.1, 5.5, 0.9, -3.3, -2.2, 4.4, 1.1};
    std::size_t bodies = 0;
    std::size_t interior = 0;
    double worstPressure = 0.0;
    double worstVelocity = 0.0;

    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        const std::vector<CellIndex> inside = interiorCells(*body.topology);
        if (inside.empty()) {
            continue;
        }

        // A no-slip wall everywhere: velocity FixedValue at ZERO, pressure
        // ZeroGradient. Both are inconsistent with the linear fields below, and
        // an interior cell must not care.
        const PreparedCfdBoundary boundary = body.wallEverywhere();

        const std::vector<double> pressures = linearCellValues(*body.geometry, gradient, 0.0);
        const Result<CfdPressureGradientField> pressure = cfd::reconstructCfdPressureGradient(
            *body.topology, *body.geometry, boundary,
            pressureInput(pressures, CfdGradientMethod::LeastSquares));
        INFO((pressure.has_value() ? std::string{} : pressure.error().message));
        REQUIRE(pressure.has_value());

        const std::vector<double> velocities =
            linearVelocity(*body.geometry, tensor, Vec{0.0, 0.0, 0.0});

        for (const CellIndex cell : inside) {
            ++interior;
            INFO("interior cell " << cell.value());
            const std::optional<CfdPressureGradientField::Vector> g = pressure->at(cell);
            REQUIRE(g.has_value());
            const Vec got{g->x.si(), g->y.si(), g->z.si()};
            worstPressure = std::max(worstPressure, normOf(subOf(got, gradient)) / normOf(gradient));
            CHECK(normOf(subOf(got, gradient)) / normOf(gradient) < kExactOnAReferenceBody);

            // AND THE WHOLE VELOCITY TENSOR, row by row, against the tensor the
            // field was built from -- an independent value, not another
            // reconstruction.
            const Mat reconstructed = velocityTensor(*body.topology, *body.geometry, boundary,
                                                     velocities, CfdGradientMethod::LeastSquares, cell);
            const double scale = worstEntryOf(tensor);
            for (std::size_t k = 0; k < 9; ++k) {
                worstVelocity = std::max(worstVelocity, std::abs(reconstructed[k] - tensor[k]) / scale);
            }
            for (std::size_t k = 0; k < 9; ++k) {
                INFO("entry " << k << ": " << reconstructed[k] << " against " << tensor[k]);
                CHECK(std::abs(reconstructed[k] - tensor[k]) / scale < kExactOnAReferenceBody);
            }
        }
    }
    INFO("bodies " << bodies << ", interior cells " << interior << ", worst pressure error "
                   << worstPressure << ", worst velocity entry error " << worstVelocity);
    CHECK(bodies == 8);
    // THE NON-TRIVIALITY GUARD, and it is the whole test: if no reference body
    // had an interior cell, every assertion above would be skipped and this case
    // would pass having checked nothing.
    CHECK(interior > 0);
}

TEST_CASE("CfdGradient_TheTwoMethodsAgreeWhereBothAreExact", "[cfd][grad]") {
    // A CROSS-CHECK BETWEEN TWO INDEPENDENT IMPLEMENTATIONS of the same
    // quantity, which is the strongest validation available here and was absent
    // until the author's own adversarial pass asked for it.
    //
    // On the one-cell tet with every facet prescribing the linear field's own
    // value, Green-Gauss has nothing to interpolate (so it is exact by the
    // volume identity) and least squares has four consistent Dirichlet rows (so
    // it is exact by construction). They reach that answer by completely
    // different arithmetic -- a surface sum over four faces against a 3x3
    // cofactor solve -- so their agreement is not a shared defect.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.internalFaces() == 0);
    const Vec gradient{7.2, -3.5, 11.4};
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, 0.0);

    const std::vector<double> values = linearCellValues(hand.geometry, gradient, 0.0);
    const Result<CfdPressureGradientField> gauss = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::GreenGauss));
    const Result<CfdPressureGradientField> squares = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::LeastSquares));
    REQUIRE(gauss.has_value());
    REQUIRE(squares.has_value());

    const std::optional<CfdPressureGradientField::Vector> a = gauss->at(CellIndex::fromValue(0));
    const std::optional<CfdPressureGradientField::Vector> b = squares->at(CellIndex::fromValue(0));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const Vec one{a->x.si(), a->y.si(), a->z.si()};
    const Vec two{b->x.si(), b->y.si(), b->z.si()};
    INFO("Green-Gauss " << one[0] << " " << one[1] << " " << one[2]);
    INFO("least squares " << two[0] << " " << two[1] << " " << two[2]);
    // Each against the exact answer...
    CHECK(normOf(subOf(one, gradient)) / normOf(gradient) < kExactOnAFixture);
    CHECK(normOf(subOf(two, gradient)) / normOf(gradient) < kExactOnAFixture);
    // ...and against each other, which is the independent half.
    CHECK(normOf(subOf(one, two)) / normOf(gradient) < kExactOnAFixture);
    // AND THEY ARE NOT THE SAME CODE PATH, which the summaries say: one reports
    // a skewness and no conditioning, the other the reverse.
    CHECK(gauss->summary().worstConditioning == 0.0);
    CHECK(squares->summary().worstConditioning >= 1.0);
}

TEST_CASE("CfdGradient_LargestMagnitudeIsTheLargestMagnitudeOverTheWholeField", "[cfd][grad]") {
    // THE AUTHOR'S OWN PASS, FINDING A6. Three cases used `largestMagnitude`,
    // all asserting `== 0.0` or `> 1.0e4`. An implementation that returned the
    // FIRST cell's magnitude, or the largest single COMPONENT rather than the
    // vector norm, would have passed every one of them.
    const Vec gradient{7.2, -3.5, 11.4};
    std::size_t bodies = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        // A WALL boundary, deliberately inconsistent with the linear field, so
        // the gradients VARY from cell to cell and a maximum is a real choice
        // rather than the same number everywhere.
        const PreparedCfdBoundary boundary = body.wallEverywhere();
        const std::vector<double> values = linearCellValues(*body.geometry, gradient, 0.0);
        const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
            *body.topology, *body.geometry, boundary,
            pressureInput(values, CfdGradientMethod::LeastSquares));
        REQUIRE(field.has_value());

        double expected = 0.0;
        double smallest = std::numeric_limits<double>::infinity();
        double largestComponent = 0.0;
        std::size_t distinct = 0;
        double first = -1.0;
        for (std::size_t c = 0; c < field->cells(); ++c) {
            const std::optional<CfdPressureGradientField::Vector> g = field->at(CellIndex::fromValue(c));
            REQUIRE(g.has_value());
            const double magnitude = std::hypot(g->x.si(), g->y.si(), g->z.si());
            expected = std::max(expected, magnitude);
            smallest = std::min(smallest, magnitude);
            largestComponent = std::max(
                {largestComponent, std::abs(g->x.si()), std::abs(g->y.si()), std::abs(g->z.si())});
            if (c == 0) {
                first = magnitude;
            } else if (std::abs(magnitude - first) > 1.0e-9 * std::max(first, 1.0)) {
                ++distinct;
            }
        }
        INFO("expected " << expected << ", reported " << field->largestMagnitude().si()
                         << ", smallest cell " << smallest << ", first cell " << first
                         << ", largest component " << largestComponent);
        CHECK_THAT(field->largestMagnitude().si(), WithinRel(expected, 1.0e-15));
        // THE DISCRIMINATIONS, each asserted rather than assumed.
        //
        // NOT "greater than the first cell's": that was the first version and it
        // FAILED on RM-MESH-01, where the first cell happens to BE the maximum.
        // The assumption was the test's, not a defect, and the honest form of
        // the claim is that the field varies and the maximum is over all of it.
        CHECK(distinct > 0);
        CHECK(field->largestMagnitude().si() > smallest);
        // And it is a VECTOR NORM and not the largest single component, which an
        // implementation maximising over components would get wrong.
        CHECK(field->largestMagnitude().si() > largestComponent);
    }
    CHECK(bodies == 8);
}

// `CfdGradient_TheConditioningOfARefusedCellIsMeasuredAndFarPastTheLimit` WAS
// HERE, AND ITS ORACLE WAS NOT INDEPENDENT. Both reviews found that its
// `ratioOracle` was a line-for-line transcription of `eigenvalueRatio` -- same
// variable names, same `detB` expansion, same `mean + 2p cos(phi)` -- so the one
// defect it was there to exclude, a transposition in the closed form, is exactly
// what it would have mirrored. Two small divergences from the original (a
// dropped finiteness guard, a bare `1.0` where production returns
// `mean > 0 ? 1.0 : inf`) confirmed it had been typed over rather than derived.
//
// It is replaced, in the part below, by a CYCLIC JACOBI diagonalisation that
// shares no arithmetic with the closed form, and by a case that brackets the
// limit from both sides using it.

TEST_CASE("CfdGradient_ASlipWallGradientMatchesAFitBuiltFromTheProjectionOracle", "[cfd][grad]") {
    // THE SWEEP'S SECOND PASS FORCED THIS CASE. The slip-wall case before it
    // asserted that changing the OTHER two velocity components MOVES the
    // gradient of the one being differentiated -- which is the coupling, and is
    // true. But it survived a mutation that hard-codes the normal's component
    // index to `0`: with the wrong index there is still coupling, so the answer
    // still moves, just to the wrong place. Proving that something moves is not
    // proving where it moves to.
    //
    // So the whole fit is built here from the projection oracle
    // `U_b = U_P - (U_P . n) n`, in the test's own arithmetic, and the operator
    // must land on it. A wrong component index changes `U_b,i` at every facet
    // and therefore every right-hand side.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::Slip).velocity == CfdBoundaryPolicy::Symmetry);
    const std::size_t cells = hand.topology.cells();
    REQUIRE(cells == 1);

    const Vec velocity{3.0, 7.0, -5.0};
    std::vector<double> values(3 * cells, 0.0);
    for (std::size_t i = 0; i < 3; ++i) {
        values[i * cells + 0] = velocity[i];
    }

    // THE ORACLE: four boundary rows, direction `C_f - C_P`, value
    // `U_b,i - U_P,i` with `U_b` from the projection, weighted by `1/|d|^2`, and
    // solved by cofactors. Written out again, so the operator's own arithmetic
    // is not the authority on its own answer.
    const auto fitFor = [&](std::size_t component) {
        std::array<double, 6> m{};
        Vec rhs{};
        for (const FaceIndex face : hand.topology.cellFaces(CellIndex::fromValue(0))) {
            const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
            REQUIRE(normal.has_value());
            const Vec unitNormal{normal->x(), normal->y(), normal->z()};
            const Vec projected = slipOracle(velocity, unitNormal);
            const Vec d = siOf(hand.geometry.cellToFace(face));
            const double w = 1.0 / dotOf(d, d);
            const double value = projected[component] - velocity[component];
            m[0] += w * d[0] * d[0];
            m[1] += w * d[0] * d[1];
            m[2] += w * d[0] * d[2];
            m[3] += w * d[1] * d[1];
            m[4] += w * d[1] * d[2];
            m[5] += w * d[2] * d[2];
            for (std::size_t i = 0; i < 3; ++i) {
                rhs[i] += w * d[i] * value;
            }
        }
        const double a = m[0];
        const double b = m[1];
        const double c = m[2];
        const double d = m[3];
        const double e = m[4];
        const double f = m[5];
        const double c00 = d * f - e * e;
        const double c01 = c * e - b * f;
        const double c02 = b * e - c * d;
        const double det = a * c00 + b * c01 + c * c02;
        REQUIRE(det > 0.0);
        const double c11 = a * f - c * c;
        const double c12 = b * c - a * e;
        const double c22 = a * d - b * b;
        return Vec{(c00 * rhs[0] + c01 * rhs[1] + c02 * rhs[2]) / det,
                   (c01 * rhs[0] + c11 * rhs[1] + c12 * rhs[2]) / det,
                   (c02 * rhs[0] + c12 * rhs[1] + c22 * rhs[2]) / det};
    };

    std::size_t discriminated = 0;
    for (std::size_t which = 0; which < 3; ++which) {
        INFO("component " << which);
        const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
            hand.topology, hand.geometry, boundary,
            velocityInput(values, cfd::kVectorComponents[which], CfdGradientMethod::LeastSquares));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        const std::optional<CfdVelocityGradientField::Vector> g = field->at(CellIndex::fromValue(0));
        REQUIRE(g.has_value());
        const Vec got{g->x.si(), g->y.si(), g->z.si()};
        const Vec expected = fitFor(which);
        const double scale = std::max(normOf(expected), 1.0e-300);
        INFO("operator " << got[0] << " " << got[1] << " " << got[2]);
        INFO("oracle   " << expected[0] << " " << expected[1] << " " << expected[2]);
        CHECK(normOf(subOf(got, expected)) / scale < kExactOnAFixture);

        // AND THE DISCRIMINATION: the fit for component `which` must differ from
        // the fit that would result from reading the normal's X component
        // instead. Where it does not, this case cannot tell the two apart, and
        // for component X it provably cannot -- so the count is asserted rather
        // than assumed.
        if (which != 0) {
            const Vec wrongIndex = fitFor(0);
            if (normOf(subOf(expected, wrongIndex)) / scale > 1.0e-6) {
                ++discriminated;
            }
        }
    }
    // Two of the three components must be distinguishable from the hard-coded-X
    // reading. Component X cannot be, by construction.
    INFO("components distinguishable from a hard-coded X index: " << discriminated);
    CHECK(discriminated == 2);
}

TEST_CASE("CfdGradient_GreenGaussMatchesASurfaceSumBuiltWithTheNormalProjectionWeight", "[cfd][grad]") {
    // THE THIRD SWEEP FORCED THIS CASE, and it closes the last real gap the
    // sweeps found.
    //
    // Green-Gauss's own arithmetic was pinned only in two places, and neither
    // could see its WEIGHT. The orthogonal-pair section requires exactness,
    // but there `d` is parallel to `n`, so `|d|` and `d . n` are the same number
    // and a weight dividing by the wrong one is a NO-OP. The skewed section
    // requires Green-Gauss to be measurably INEXACT -- and a wrong weight is
    // also inexact, just differently, so it passed too.
    //
    // So the whole surface sum is built here, on the NON-ORTHOGONAL analytic
    // pair, with the normal-projection weight and the stable interpolation, and
    // the operator must land on it. The oracle is also computed with the WRONG
    // weight and the two are required to differ, so the case is known to
    // discriminate rather than assumed to.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const FaceIndex internal = FaceIndex::fromValue(0);
    REQUIRE(hand.topology.isInternal(internal));

    const Vec gradient{7.2, -3.5, 11.4};
    const double offset = 305.7;
    const std::vector<double> values = linearCellValues(hand.geometry, gradient, offset);
    const PreparedCfdBoundary boundary = agreeingPressureBoundary(hand, gradient, offset);

    // THE WEIGHT IS NOT ONE HALF ON THIS FIXTURE, which is asserted because it
    // is what makes the weight observable at all: at exactly one half both
    // candidate forms of the interpolation agree to the last bit.
    const double weight = weightOracle(hand.geometry, internal);
    INFO("the owner's interpolation weight on the internal face: " << weight);
    CHECK(weight > 0.0);
    CHECK(weight < 1.0);

    // THE ORACLE: `(1/V) sum_f phi_f S_f`, with the sign read from `owner`, the
    // boundary value taken from the prescribed pressure, and the internal value
    // interpolated with @p useNormalProjection deciding which denominator.
    const auto gaussOracle = [&](std::size_t cellIndex, bool useNormalProjection) {
        const CellIndex cell = CellIndex::fromValue(cellIndex);
        Vec accumulated{0.0, 0.0, 0.0};
        for (const FaceIndex face : hand.topology.cellFaces(cell)) {
            const CellIndex owner = hand.topology.owner(face);
            const bool isOwner = owner.value() == cellIndex;
            const Vec stored = siOf(hand.geometry.faceAreaVector(face));
            const Vec outward = isOwner ? stored : Vec{-stored[0], -stored[1], -stored[2]};

            double faceValue = 0.0;
            if (hand.topology.isInternal(face)) {
                const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
                REQUIRE(neighbour.has_value());
                const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
                REQUIRE(normal.has_value());
                const Vec unitNormal{normal->x(), normal->y(), normal->z()};
                const Vec span = siOf(*hand.geometry.ownerToNeighbour(face));
                // THE DENOMINATOR IS THE WHOLE QUESTION: `d . n` is right and
                // `|d|` is the plausible wrong answer.
                const double total = useNormalProjection ? dotOf(span, unitNormal)
                                                         : std::sqrt(dotOf(span, span));
                const double ownerPart = dotOf(siOf(hand.geometry.cellToFace(face)), unitNormal);
                const double w = (total - ownerPart) / total;
                const double ownerValue = values[owner.value()];
                const double neighbourValue = values[neighbour->value()];
                faceValue = neighbourValue + w * (ownerValue - neighbourValue);
            } else {
                const std::optional<BoundaryFaceIndex> slot = hand.topology.boundaryFaceOf(face);
                REQUIRE(slot.has_value());
                const std::optional<cfd::CfdBoundaryFace> facet = boundary.face(*slot);
                REQUIRE(facet.has_value());
                REQUIRE(facet->pressure.has_value());
                faceValue = facet->pressure->si();
            }
            accumulated[0] += outward[0] * faceValue;
            accumulated[1] += outward[1] * faceValue;
            accumulated[2] += outward[2] * faceValue;
        }
        const double volume = hand.geometry.cellVolume(cell).si();
        REQUIRE(volume > 0.0);
        return Vec{accumulated[0] / volume, accumulated[1] / volume, accumulated[2] / volume};
    };

    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::GreenGauss));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    double worstAgreement = 0.0;
    double smallestDisagreement = std::numeric_limits<double>::infinity();
    for (std::size_t c = 0; c < field->cells(); ++c) {
        INFO("cell " << c);
        const std::optional<CfdPressureGradientField::Vector> g = field->at(CellIndex::fromValue(c));
        REQUIRE(g.has_value());
        const Vec got{g->x.si(), g->y.si(), g->z.si()};
        const Vec right = gaussOracle(c, true);
        const Vec wrong = gaussOracle(c, false);
        const double scale = std::max(normOf(right), 1.0e-300);
        INFO("operator        " << got[0] << " " << got[1] << " " << got[2]);
        INFO("d . n oracle    " << right[0] << " " << right[1] << " " << right[2]);
        INFO("|d|   oracle    " << wrong[0] << " " << wrong[1] << " " << wrong[2]);
        worstAgreement = std::max(worstAgreement, normOf(subOf(got, right)) / scale);
        smallestDisagreement = std::min(smallestDisagreement, normOf(subOf(right, wrong)) / scale);
    }
    INFO("agreement with the d . n form " << worstAgreement << ", distance between the two forms "
                                          << smallestDisagreement);
    // The operator agrees with the normal-projection form to rounding...
    CHECK(worstAgreement < 1.0e-13);
    // ...and the two forms are genuinely different on this fixture, so the
    // agreement above is a choice and not a coincidence.
    CHECK(smallestDisagreement > 1.0e-3);
}

namespace {

/// The extreme eigenvalue ratio of a symmetric 3x3, by CYCLIC JACOBI ROTATION.
///
/// GENUINELY INDEPENDENT, AND THE EARLIER ORACLE WAS NOT. A test-gap review and
/// an adversarial review both found that this file's first conditioning oracle
/// was a line-for-line transcription of `eigenvalueRatio` -- same variable
/// names, same `detB` expansion, same `mean + 2p cos(phi)` -- while the file's
/// own header claimed its oracles were "written out again... so a transposition
/// in production cannot be mirrored here". It would have mirrored exactly such a
/// transposition. The concrete defect it could not see: writing
/// `cos(phi - 2*pi/3)` for the smallest eigenvalue yields the MIDDLE one for
/// `phi` in `[0, pi/3]`, so the operator would report `lambda_max / lambda_mid`
/// -- not the 2-norm condition number ADR-070 Decision 7 claims -- while still
/// being `>= 1`, still blowing up on a flat cell, and still passing every
/// inequality in the suite.
///
/// Jacobi shares no arithmetic with the closed form: it diagonalises by repeated
/// plane rotations and reads the eigenvalues off the diagonal. No `acos`, no
/// characteristic cubic.
[[nodiscard]] double jacobiRatio(const std::array<double, 6>& packed) {
    // [[a,b,c],[b,d,e],[c,e,f]] as a full 3x3, so the rotations are plain.
    std::array<std::array<double, 3>, 3> m{{{packed[0], packed[1], packed[2]},
                                            {packed[1], packed[3], packed[4]},
                                            {packed[2], packed[4], packed[5]}}};
    const auto offDiagonalNorm = [&] {
        return std::abs(m[0][1]) + std::abs(m[0][2]) + std::abs(m[1][2]);
    };
    const double scale = std::max({std::abs(m[0][0]), std::abs(m[1][1]), std::abs(m[2][2]), 1.0e-300});
    for (int sweep = 0; sweep < 100 && offDiagonalNorm() > 1.0e-18 * scale; ++sweep) {
        for (const auto& pair : std::array<std::array<std::size_t, 2>, 3>{{{0, 1}, {0, 2}, {1, 2}}}) {
            const std::size_t p = pair[0];
            const std::size_t q = pair[1];
            if (std::abs(m[p][q]) <= 1.0e-300) {
                continue;
            }
            const double theta = 0.5 * std::atan2(2.0 * m[p][q], m[q][q] - m[p][p]);
            const double cs = std::cos(theta);
            const double sn = std::sin(theta);
            std::array<std::array<double, 3>, 3> next = m;
            for (std::size_t k = 0; k < 3; ++k) {
                next[p][k] = cs * m[p][k] - sn * m[q][k];
                next[q][k] = sn * m[p][k] + cs * m[q][k];
            }
            std::array<std::array<double, 3>, 3> after = next;
            for (std::size_t k = 0; k < 3; ++k) {
                after[k][p] = cs * next[k][p] - sn * next[k][q];
                after[k][q] = sn * next[k][p] + cs * next[k][q];
            }
            m = after;
        }
    }
    const double smallest = std::min({m[0][0], m[1][1], m[2][2]});
    const double largest = std::max({m[0][0], m[1][1], m[2][2]});
    if (!(smallest > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    return largest / smallest;
}

/// The least-squares normal matrix for @p cell with every boundary face taken as
/// `ZeroGradient` and every internal face as a value row.
///
/// Built from the geometry in the test's own terms. Its CONSTRUCTION is already
/// pinned end to end by the linear-exactness cases -- a wrong direction or
/// weight there changes the gradient and those cases fail -- so what this adds
/// is an independent reading of the matrix's CONDITIONING.
[[nodiscard]] std::array<double, 6> normalMatrixOracle(const CfdTopology& topology,
                                                       const CfdGeometry& geometry, std::size_t cell) {
    std::array<double, 6> m{};
    for (const FaceIndex face : topology.cellFaces(CellIndex::fromValue(cell))) {
        Vec d{};
        if (topology.isInternal(face)) {
            const std::optional<Translation3D> span = geometry.ownerToNeighbour(face);
            REQUIRE(span.has_value());
            d = siOf(*span);
            if (topology.owner(face).value() != cell) {
                d = Vec{-d[0], -d[1], -d[2]};
            }
        } else {
            const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
            REQUIRE(normal.has_value());
            const double h = geometry.orthogonalDistance(face).si();
            d = Vec{normal->x() * h, normal->y() * h, normal->z() * h};
        }
        const double squared = dotOf(d, d);
        REQUIRE(squared > 0.0);
        const double w = 1.0 / squared;
        m[0] += w * d[0] * d[0];
        m[1] += w * d[0] * d[1];
        m[2] += w * d[0] * d[2];
        m[3] += w * d[1] * d[1];
        m[4] += w * d[1] * d[2];
        m[5] += w * d[2] * d[2];
    }
    return m;
}

} // namespace

TEST_CASE("CfdGradient_TheReportedConditioningMatchesAJacobiDiagonalisation", "[cfd][grad]") {
    // BOTH REVIEWS FOUND THIS GAP INDEPENDENTLY, and it was the sharpest thing
    // either of them said about the suite. `worstConditioning` was only ever
    // BOUNDED -- `>= 1.0`, `< 1e12`, `< 1e9`, `== 0.0` for Green-Gauss -- and
    // never compared against a value. It is the one number ADR-070 Decision 7
    // hands to P19-VALID-001 to set a mesh-acceptance threshold against, so a
    // silent factor in it would propagate into a later milestone's judgement.
    //
    // The oracle here is a cyclic Jacobi diagonalisation, which shares no
    // arithmetic with the closed-form trigonometric method under test.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    // A no-slip wall: the PRESSURE column is ZeroGradient at every facet, which
    // is exactly the configuration `normalMatrixOracle` builds.
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::NoSlipWall).pressure ==
            CfdBoundaryPolicy::ZeroGradient);
    REQUIRE(hand.topology.cells() == 1);

    const std::vector<double> values(hand.topology.cells(), 11.0);
    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::LeastSquares));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    const std::array<double, 6> m = normalMatrixOracle(hand.topology, hand.geometry, 0);
    const double jacobi = jacobiRatio(m);
    const double reported = field->summary().worstConditioning;
    INFO("Jacobi " << jacobi << ", reported " << reported);
    REQUIRE(std::isfinite(jacobi));
    // THE VALUE, not a bound. A factor of two, or a middle eigenvalue in place
    // of the smallest, dies here.
    CHECK_THAT(reported, WithinRel(jacobi, 1.0e-9));
    // And the number is not trivially 1, so the agreement is not two ways of
    // reporting a perfectly conditioned matrix.
    CHECK(jacobi > 1.5);
}

TEST_CASE("CfdGradient_TheReportedConditioningIsTheWorstOverTheMeshAndNotTheLast", "[cfd][grad]") {
    // THE REDUCTION, pinned. A test-gap review proposed replacing
    // `worstConditioning = max(worstConditioning, ratio)` with plain assignment
    // -- reporting the LAST cell's ratio -- and showed every assertion in the
    // suite is an inequality any cell's ratio satisfies. That mutation is dead
    // here: the maximum over the mesh is computed independently, and the last
    // cell's value is computed too and required to DIFFER from it, so the two
    // cannot be confused.
    std::size_t bodies = 0;
    std::size_t discriminating = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        const PreparedCfdBoundary boundary = body.wallEverywhere();
        const std::vector<double> values(body.topology->cells(), 11.0);
        const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
            *body.topology, *body.geometry, boundary,
            pressureInput(values, CfdGradientMethod::LeastSquares));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        double worst = 0.0;
        double last = 0.0;
        for (std::size_t c = 0; c < body.topology->cells(); ++c) {
            const double ratio = jacobiRatio(normalMatrixOracle(*body.topology, *body.geometry, c));
            REQUIRE(std::isfinite(ratio));
            worst = std::max(worst, ratio);
            last = ratio;
        }
        const double reported = field->summary().worstConditioning;
        INFO("Jacobi worst " << worst << ", Jacobi last cell " << last << ", reported " << reported);
        CHECK_THAT(reported, WithinRel(worst, 1.0e-9));
        // THE DISCRIMINATION: on a body where the last cell is not the worst,
        // the two numbers differ and a plain assignment would report the wrong
        // one.
        if (std::abs(worst - last) > 1.0e-6 * worst) {
            ++discriminating;
        }
    }
    INFO("bodies " << bodies << ", bodies where the last cell is NOT the worst: " << discriminating);
    CHECK(bodies == 8);
    // At least one body must discriminate, or the equality above is satisfied by
    // the max and the last being the same cell everywhere.
    CHECK(discriminating >= 1);
}

TEST_CASE("CfdGradient_TheReportedSkewnessIsTheWorstOverSeveralInterpolatedFaces", "[cfd][grad]") {
    // THE OTHER REDUCTION, and it had the same hole. `worstSkewness`'s VALUE was
    // pinned exactly once, on the analytic pair -- which has exactly ONE
    // internal face, so "the worst over the mesh" and "the last face visited"
    // are the same number there. A test-gap review proposed plain assignment and
    // showed it survives.
    //
    // A reference body has hundreds of internal faces, so the maximum is a real
    // choice. It is computed here from `CfdGeometry::skewness` directly.
    std::size_t bodies = 0;
    std::size_t discriminating = 0;
    std::size_t totalInternal = 0;
    const Vec gradient{7.2, -3.5, 11.4};
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++bodies;
        const PreparedCfdBoundary boundary = agreeingReferenceBoundary(body, gradient, 0.0);
        const Reading gauss = readPressureGradient(*body.topology, *body.geometry, boundary, gradient,
                                                   0.0, CfdGradientMethod::GreenGauss);

        double worst = 0.0;
        double last = 0.0;
        std::size_t internal = 0;
        for (std::size_t f = 0; f < body.topology->faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            if (!body.topology->isInternal(face)) {
                continue;
            }
            const std::optional<double> skew = body.geometry->skewness(face);
            REQUIRE(skew.has_value());
            worst = std::max(worst, std::abs(*skew));
            last = std::abs(*skew);
            ++internal;
        }
        INFO("internal faces " << internal << ", independently measured worst skewness " << worst
                               << ", last " << last << ", reported "
                               << gauss.summary.worstSkewness);
        // MORE THAN ONE interpolated face, so the maximum is a choice. The
        // per-body threshold was `> 10` first and RM-MESH-01 has exactly 6, so
        // the figure that carries the claim is the aggregate below, not this.
        CHECK(internal > 1);
        CHECK_THAT(gauss.summary.worstSkewness, WithinRel(worst, 1.0e-12));
        totalInternal += internal;
        if (std::abs(worst - last) > 1.0e-6 * worst) {
            ++discriminating;
        }
    }
    INFO("bodies " << bodies << ", internal faces in all " << totalInternal
                   << ", bodies where the last internal face is NOT the worst: " << discriminating);
    CHECK(bodies == 8);
    // THE AGGREGATE IS THE NON-TRIVIALITY GUARD: thousands of interpolated
    // faces, so a maximum over them is a real reduction.
    CHECK(totalInternal > 1000);
    CHECK(discriminating >= 1);
}

TEST_CASE("CfdGradient_GreenGaussTakesTheOwnersValueAcrossAZeroGradientFacet", "[cfd][grad]") {
    // A TEST-GAP REVIEW FOUND THIS ARM COMPLETELY DEAD. `boundaryValue`'s
    // `ZeroGradient` case is reached only by Green-Gauss, because least squares
    // intercepts that policy earlier and writes a constraint row instead -- and
    // EVERY Green-Gauss reconstruction in this file ran on a boundary where the
    // differentiated field is `FixedValue`. The review checked all twelve call
    // sites. So the documented behaviour, that a zero normal derivative means
    // the face takes the owner's own value, had no coverage at all, and
    // replacing `view.at(owner)` with `0.0` survived the whole suite.
    //
    // A no-slip wall gives the PRESSURE column `ZeroGradient` at every facet,
    // which is the configuration that reaches it.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::NoSlipWall).pressure ==
            CfdBoundaryPolicy::ZeroGradient);

    const std::vector<double> values{11.0, 29.0};
    REQUIRE(values.size() == hand.topology.cells());

    const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
        hand.topology, hand.geometry, boundary, pressureInput(values, CfdGradientMethod::GreenGauss));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    CHECK(field->summary().zeroGradientFaces == hand.topology.boundaryFaces());
    CHECK(field->summary().fixedValueFaces == 0);

    // THE ORACLE: the surface sum with the owner's own value at every boundary
    // facet, and the interpolation at the one internal face. Built here, and
    // also built with a hard-coded `0.0` at the boundary, with the two required
    // to differ -- so the case is known to discriminate.
    const auto oracle = [&](std::size_t cellIndex, bool ownersValue) {
        const CellIndex cell = CellIndex::fromValue(cellIndex);
        Vec accumulated{};
        for (const FaceIndex face : hand.topology.cellFaces(cell)) {
            const CellIndex owner = hand.topology.owner(face);
            const Vec stored = siOf(hand.geometry.faceAreaVector(face));
            const Vec outward =
                owner.value() == cellIndex ? stored : Vec{-stored[0], -stored[1], -stored[2]};
            double faceValue = 0.0;
            if (hand.topology.isInternal(face)) {
                const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
                REQUIRE(neighbour.has_value());
                const double w = weightOracle(hand.geometry, face);
                faceValue = values[neighbour->value()] +
                            w * (values[owner.value()] - values[neighbour->value()]);
            } else {
                faceValue = ownersValue ? values[owner.value()] : 0.0;
            }
            accumulated[0] += outward[0] * faceValue;
            accumulated[1] += outward[1] * faceValue;
            accumulated[2] += outward[2] * faceValue;
        }
        const double volume = hand.geometry.cellVolume(cell).si();
        REQUIRE(volume > 0.0);
        return Vec{accumulated[0] / volume, accumulated[1] / volume, accumulated[2] / volume};
    };

    double worstAgreement = 0.0;
    double smallestDisagreement = std::numeric_limits<double>::infinity();
    for (std::size_t c = 0; c < field->cells(); ++c) {
        INFO("cell " << c);
        const std::optional<CfdPressureGradientField::Vector> g = field->at(CellIndex::fromValue(c));
        REQUIRE(g.has_value());
        const Vec got{g->x.si(), g->y.si(), g->z.si()};
        const Vec right = oracle(c, true);
        const Vec zeroed = oracle(c, false);
        const double scale = std::max(normOf(right), 1.0e-300);
        INFO("operator " << got[0] << " " << got[1] << " " << got[2]);
        INFO("owner's-value oracle " << right[0] << " " << right[1] << " " << right[2]);
        INFO("hard-zero oracle     " << zeroed[0] << " " << zeroed[1] << " " << zeroed[2]);
        worstAgreement = std::max(worstAgreement, normOf(subOf(got, right)) / scale);
        smallestDisagreement = std::min(smallestDisagreement, normOf(subOf(right, zeroed)) / scale);
    }
    INFO("agreement " << worstAgreement << ", distance from the hard-zero form "
                      << smallestDisagreement);
    CHECK(worstAgreement < 1.0e-13);
    CHECK(smallestDisagreement > 1.0e-3);
}

TEST_CASE("CfdGradient_GreenGaussReconstructsAFlatCellThatLeastSquaresRefuses", "[cfd][grad]") {
    // THE OTHER HALF OF THE CONDITIONING GATE'S DOCUMENTED SCOPE. Phase 8 runs
    // for least squares ONLY, and a test-gap review showed that deleting that
    // guard -- running the gate for Green-Gauss too -- changes no test outcome,
    // because every Green-Gauss mesh in the suite is well conditioned. So
    // "least squares only" had no coverage.
    //
    // Green-Gauss solves nothing, so it has no normal matrix to condition and
    // must reconstruct a cell least squares refuses. That is the asymmetry the
    // guard exists for, and it is asserted here from both sides.
    const auto flatPoints = [](double thickness) {
        const auto p = [](double x, double y, double z) {
            return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
        };
        return std::vector<Point3D>{p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0.3, 0.3, thickness)};
    };

    std::size_t refusedBySquares = 0;
    std::size_t solvedByGauss = 0;
    for (const double thickness : {1.0e-9, 1.0e-12}) {
        INFO("thickness " << thickness);
        const meshing::Mesh mesh = buildMesh(flatPoints(thickness), kOneCell);
        const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
        REQUIRE(topology.has_value());
        const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
        REQUIRE(geometry.has_value());

        std::vector<ElementId> ascending;
        for (std::size_t b = 0; b < topology->boundaryFaces(); ++b) {
            ascending.push_back(topology->triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        const CfdResolvedTarget target{
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}},
            ascending};
        const std::array<CfdResolvedTarget, 1> targets{target};
        const std::optional<Point3D> centroid = geometry->cellCentroid(CellIndex::fromValue(0));
        REQUIRE(centroid.has_value());
        const cfd::CfdReferencePressure datum{.value = 0_Pa, .location = *centroid};
        const Result<PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            *topology, *geometry, mesh, targets, 0, datum, std::nullopt, std::nullopt);
        REQUIRE(bound.has_value());

        const std::vector<double> values{11.0};
        const Result<CfdPressureGradientField> squares = cfd::reconstructCfdPressureGradient(
            *topology, *geometry, *bound, pressureInput(values, CfdGradientMethod::LeastSquares));
        const Result<CfdPressureGradientField> gauss = cfd::reconstructCfdPressureGradient(
            *topology, *geometry, *bound, pressureInput(values, CfdGradientMethod::GreenGauss));

        // LEAST SQUARES REFUSES IT...
        REQUIRE_FALSE(squares.has_value());
        CHECK(cfd::cfdPressureGradientProblem(
                  *topology, *geometry, *bound,
                  pressureInput(values, CfdGradientMethod::LeastSquares)) ==
              CfdGradientProblem::NeighbourhoodIsDegenerate);
        ++refusedBySquares;

        // ...AND GREEN-GAUSS DOES NOT, because it solves nothing. Its answer on
        // such a cell is not claimed to be useful; what is claimed is that the
        // conditioning gate is least squares' and not the operator's.
        INFO((gauss.has_value() ? std::string{} : gauss.error().message));
        REQUIRE(gauss.has_value());
        CHECK(gauss->summary().worstConditioning == 0.0);
        CHECK(std::isfinite(gauss->largestMagnitude().si()));
        CHECK_FALSE(cfd::cfdPressureGradientProblem(
                        *topology, *geometry, *bound,
                        pressureInput(values, CfdGradientMethod::GreenGauss))
                        .has_value());
        ++solvedByGauss;
    }
    INFO("refused by least squares " << refusedBySquares << ", reconstructed by Green-Gauss "
                                     << solvedByGauss);
    CHECK(refusedBySquares == 2);
    CHECK(solvedByGauss == 2);
}

TEST_CASE("CfdGradient_ANonFiniteGradientIsRefusedRatherThanReturned", "[cfd][grad]") {
    // AN ADVERSARIAL REVIEW FOUND THE GREEN-GAUSS PATH RETURNING A NaN AS A
    // SUCCESS. Phase 4 requires every cell value to be FINITE, which admits
    // 1.8e308; nothing then checked the ANSWER. A surface sum over faces of
    // opposite orientation overflows to infinities of each sign, cancels to a
    // NaN, passes `volume > 0`, and reaches a caller through `at()` and
    // `largestMagnitude()` with no error at all.
    //
    // `thermal` and `structural` both refuse a non-finite gradient rather than
    // propagate it. This operator now does too, and this case is why the
    // enumerator exists.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    // Finite, and enormous: these pass phase 4 by construction.
    const double huge = 1.0e308;
    const std::vector<double> values{huge, -huge};
    REQUIRE(values.size() == hand.topology.cells());
    for (const double value : values) {
        REQUIRE(std::isfinite(value));
    }

    std::size_t refused = 0;
    for (const CfdGradientMethod method : cfd::kCfdGradientMethods) {
        INFO(cfd::toString(method));
        const Result<CfdPressureGradientField> field = cfd::reconstructCfdPressureGradient(
            hand.topology, hand.geometry, boundary, pressureInput(values, method));
        if (field.has_value()) {
            // If it succeeded, the answer must at least be FINITE -- which is
            // the property that was missing.
            INFO("largest |grad p| = " << field->largestMagnitude().si());
            CHECK(std::isfinite(field->largestMagnitude().si()));
            for (std::size_t c = 0; c < field->cells(); ++c) {
                const std::optional<CfdPressureGradientField::Vector> g =
                    field->at(CellIndex::fromValue(c));
                REQUIRE(g.has_value());
                CHECK(std::isfinite(g->x.si()));
                CHECK(std::isfinite(g->y.si()));
                CHECK(std::isfinite(g->z.si()));
            }
            continue;
        }
        ++refused;
        INFO("refused: " << field.error().message);
        CHECK(field.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(field.error().message, ContainsSubstring("finite"));

        // AND THE PREDICTOR'S REACH IS ASSERTED RATHER THAN ASSUMED, because the
        // two methods differ here and the difference is structural.
        //
        // Least squares refuses this IN THE GATE, because its solve is part of
        // the gate, so the predictor sees it and names it. Green-Gauss refuses
        // it in the ASSEMBLY, because a non-finite surface sum is a property of
        // the ANSWER and the predictor does not assemble one -- so the predictor
        // returns no problem and the reconstruction still refuses. That is a
        // real limit on what a predictor can promise, and it is recorded in the
        // evidence rather than hidden by asserting only the arm that works.
        const std::optional<CfdGradientProblem> predicted = cfd::cfdPressureGradientProblem(
            hand.topology, hand.geometry, boundary, pressureInput(values, method));
        if (method == CfdGradientMethod::LeastSquares) {
            CHECK(predicted == CfdGradientProblem::GradientIsNotFinite);
        } else {
            CHECK_FALSE(predicted.has_value());
        }
    }
    // AT LEAST ONE METHOD MUST ACTUALLY REACH THE REFUSAL, or this case is
    // asserting finiteness of answers that never overflowed.
    INFO("methods that refused: " << refused);
    CHECK(refused >= 1);
}

TEST_CASE("CfdGradient_ASlipWallLeavesTheConstantModeInexactAndTheFlagSaysSo", "[cfd][grad]") {
    // AN ADVERSARIAL REVIEW FOUND THE FLAG'S REASON TO BE FALSE, not merely
    // incomplete. `constantModeIsExact` was hard-coded `true` for least squares,
    // justified by "every right-hand side is a DIFFERENCE, and for a consistent
    // uniform field every difference is exactly 0.0". At a Symmetry face the row
    // value is `U_b,i - U_P,i = -n_i (U_P . n)`, which is NOT zero for a uniform
    // velocity that is not tangential to the wall.
    //
    // So the flag read `true` on a mesh where a uniform field reconstructs to
    // something non-zero. Both halves are asserted here: the gradient really is
    // non-zero, and the flag really does say so.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary slip =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    const std::size_t cells = hand.topology.cells();

    // A uniform velocity, NOT tangential to the slip wall.
    std::vector<double> uniform(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        uniform[0 * cells + c] = 2.0;
        uniform[1 * cells + c] = 3.0;
        uniform[2 * cells + c] = -1.0;
    }

    const Result<CfdVelocityGradientField> field = cfd::reconstructCfdVelocityGradient(
        hand.topology, hand.geometry, slip,
        velocityInput(uniform, VectorComponent::X, CfdGradientMethod::LeastSquares));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    CHECK(field->summary().symmetryFaces == hand.topology.boundaryFaces());

    // A UNIFORM FIELD DOES NOT RECONSTRUCT TO ZERO HERE, which is correct: the
    // slip wall describes a field that is not uniform.
    INFO("largest |grad u_x| = " << field->largestMagnitude().si());
    CHECK(field->largestMagnitude().si() > 1.0);
    // AND THE FLAG SAYS SO, which is what was wrong.
    CHECK_FALSE(field->summary().constantModeIsExact);

    // The same mesh with a no-slip wall has no Symmetry face, and there the flag
    // is true and a uniform field (which the wall's zero does not agree with,
    // so the gradient is non-zero) still reports an exact constant mode --
    // because the flag describes the OPERATOR and the mesh, not the input.
    const PreparedCfdBoundary wall =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdVelocityGradientField> noSlip = cfd::reconstructCfdVelocityGradient(
        hand.topology, hand.geometry, wall,
        velocityInput(uniform, VectorComponent::X, CfdGradientMethod::LeastSquares));
    REQUIRE(noSlip.has_value());
    CHECK(noSlip->summary().symmetryFaces == 0);
    CHECK(noSlip->summary().constantModeIsExact);
}

TEST_CASE("CfdGradient_TheDeclaredPhaseOrderIsTheCheckOrder", "[cfd][grad]") {
    // THE TEST THE HEADER'S static_assert NAMES. It named one that did not
    // exist -- a test-gap review found that too -- and the only ordering
    // assertion in the file was length-before-finiteness.
    //
    // WHAT CAN BE ASSERTED IS BOUNDED BY WHAT IS REACHABLE. Three of the nine
    // enumerators cannot be produced through any public door (two boundary
    // policies that ADR-067's frozen table never emits, and the scalar-Symmetry
    // guard behind them), so the order is pinned across the phases a
    // constructible input can reach, and the evidence records the rest.
    const Hand one = Hand::make(analyticPoints(), kAnalyticPair);
    const Hand single = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary good = one.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});
    const PreparedCfdBoundary wrongCount = single.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});
    REQUIRE(one.topology.boundaryFaces() != single.topology.boundaryFaces());

    const std::size_t cells = one.topology.cells();
    const std::vector<double> ok(cells, 1.0);
    std::vector<double> poisoned(cells, 1.0);
    poisoned[0] = std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> shortAndPoisoned{std::numeric_limits<double>::quiet_NaN()};
    REQUIRE(shortAndPoisoned.size() != cells);

    const auto problem = [&](const CfdGeometry& geometry, const PreparedCfdBoundary& boundary,
                             std::span<const double> values) {
        return cfd::cfdPressureGradientProblem(one.topology, geometry, boundary,
                                               pressureInput(values, CfdGradientMethod::LeastSquares));
    };

    // 1 BEFORE 2: a geometry from another mesh AND a boundary of the wrong face
    // count reports the GEOMETRY.
    CHECK(problem(single.geometry, wrongCount, ok) ==
          CfdGradientProblem::GeometryDescribesAnotherTopology);
    // 2 BEFORE 3: a wrong boundary face count AND a wrong value length reports
    // the BOUNDARY.
    CHECK(problem(one.geometry, wrongCount, shortAndPoisoned) ==
          CfdGradientProblem::BoundaryDescribesAnotherTopology);
    // 3 BEFORE 4: a wrong length AND a NaN reports the LENGTH.
    CHECK(problem(one.geometry, good, shortAndPoisoned) == CfdGradientProblem::CellValueLengthWrong);
    // 4 alone.
    CHECK(problem(one.geometry, good, poisoned) == CfdGradientProblem::CellValueNotFinite);
    // and a clean input reaches no phase at all.
    CHECK_FALSE(problem(one.geometry, good, ok).has_value());

    // 4 BEFORE 8: a NaN AND a degenerate neighbourhood reports the VALUE. The
    // flat tetrahedron is the only fixture whose neighbourhood is degenerate.
    const auto flat = [] {
        const auto p = [](double x, double y, double z) {
            return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
        };
        return std::vector<Point3D>{p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0.3, 0.3, 1.0e-12)};
    }();
    const Hand sliver = Hand::make(flat, kOneCell);
    const PreparedCfdBoundary sliverWall = sliver.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const std::vector<double> oneNaN{std::numeric_limits<double>::quiet_NaN()};
    CHECK(cfd::cfdPressureGradientProblem(
              sliver.topology, sliver.geometry, sliverWall,
              pressureInput(oneNaN, CfdGradientMethod::LeastSquares)) ==
          CfdGradientProblem::CellValueNotFinite);
    // and with a clean value, the same mesh reaches phase 8.
    const std::vector<double> oneGood{11.0};
    CHECK(cfd::cfdPressureGradientProblem(
              sliver.topology, sliver.geometry, sliverWall,
              pressureInput(oneGood, CfdGradientMethod::LeastSquares)) ==
          CfdGradientProblem::NeighbourhoodIsDegenerate);
}

TEST_CASE("CfdGradient_TheConditioningLimitIsBracketedFromBothSidesByJacobi", "[cfd][grad]") {
    // THE REPLACEMENT for the case whose oracle was a transcription. The claim
    // is the same and it is worth keeping -- a test-gap review called it the one
    // place in the suite where a numerical constant is genuinely bracketed -- but
    // the oracle is now a Jacobi diagonalisation that shares no arithmetic with
    // the closed form under test.
    //
    // Both sides of `kCfdGradientConditioningLimit` must be reached, and the
    // crossing must not be marginal: that is what says the limit refuses
    // hopeless systems rather than awkward ones, and it is what stops it being
    // a mesh-quality threshold smuggled in past ADR-063 finding F1.
    const auto flatPoints = [](double thickness) {
        const auto p = [](double x, double y, double z) {
            return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
        };
        return std::vector<Point3D>{p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0.3, 0.3, thickness)};
    };

    std::size_t below = 0;
    std::size_t above = 0;
    double worstBelow = 0.0;
    double smallestAbove = std::numeric_limits<double>::infinity();

    for (const double thickness : {1.0e-2, 1.0e-3, 1.0e-6, 1.0e-9, 1.0e-12}) {
        INFO("thickness " << thickness);
        const Hand hand = Hand::make(flatPoints(thickness), kOneCell);
        const double ratio = jacobiRatio(normalMatrixOracle(hand.topology, hand.geometry, 0));
        INFO("the Jacobi eigenvalue ratio: " << ratio);
        if (ratio <= cfd::kCfdGradientConditioningLimit) {
            ++below;
            worstBelow = std::max(worstBelow, ratio);
        } else {
            ++above;
            smallestAbove = std::min(smallestAbove, ratio);
        }
    }

    INFO("below the limit " << below << " (worst " << worstBelow << "), above it " << above
                            << " (smallest " << smallestAbove << ")");
    CHECK(below >= 1);
    CHECK(above >= 1);
    // The crossing is orders of magnitude wide, so the constant cannot be moved
    // far in either direction without a failure here. `worstBelow` is about
    // 3.6e11 -- the 1e-6-thick cell, which the operator now correctly solves and
    // which the uncorrected eigenvalue form falsely refused.
    CHECK(worstBelow < 1.0e12);
    CHECK(worstBelow > 1.0e10);
    CHECK(smallestAbove > 1.0e13);
}
