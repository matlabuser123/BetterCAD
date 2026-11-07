/// \file
/// P19-DIV-001 — finite-volume divergence / flux operator, ADR-071.
///
/// WHAT IS CHECKED WHERE, AND WHY THERE.
///
/// ```text
/// the two-cell pair     the ONLY fixture with an internal face whose two
///                       contributions can be read separately, so it is where
///                       the exact-cancellation claim is pinned
/// the one-cell tet      four boundary facets and no internal face: the only
///                       fixture where the boundary contribution is the WHOLE
///                       answer, and where a Symmetry face's exact zero is
///                       visible in the total
/// the analytic pair     |d| = 5/4, d . n = 3/4, cos(theta) = 3/5, all exact.
///                       Where a flux is pinned against closed-form arithmetic
/// the reference bodies  linear exactness, conservation, invariance and
///                       determinism, on meshes nobody designed to flatter the
///                       operator
/// ```
///
/// THE ORACLES ARE WRITTEN OUT AGAIN rather than read from the implementation.
/// The signed surface sum, the slip projection and the linear field each appear
/// here in their own arithmetic, so a transposition in production cannot be
/// mirrored here.
///
/// AND THE WEIGHT ORACLE WAS NOT, WHICH THIS CLAIM USED TO DENY. Two
/// independent reviews found `weightOracle` to be a transcription of
/// production's expression -- the same two accessors, in the same order, with
/// the same `(total - owner) / total` -- under a sentence saying the operator's
/// form could not be mirrored here. That is P19-GRAD-001's r5 defect recurring
/// one milestone later: a line-for-line copy is not validation. It is
/// re-expressed from the two cell centroids and the face centroid now, reading
/// neither `cellToFace` nor `orthogonalDistance`, and the weight's CLOSED FORM
/// on the analytic pair is asserted besides.
///
/// AND TWO ORACLES SHARE ONE THING WITH PRODUCTION ON PURPOSE: they read
/// `topology.owner(face)` and apply ADR-064's winding. So they kill any mutation
/// of the sign, and they would NOT reveal a shared misunderstanding of the
/// convention itself. `cellSurfaceVectorSum` is the independent authority there,
/// and the closure case is where it is consulted -- which it now actually is. A
/// review found that claim standing in this header while the accessor was never
/// called anywhere in the file.
///
/// WHAT THIS FILE DELIBERATELY DOES NOT ASSERT. There is no claim that global
/// conservation is bit-exact: ADR-071 Decision 3 measured that it is not, on
/// 59.66% of random two-cell meshes, so the suite bounds the imbalance against
/// the accumulated flux magnitude and reports it. A test asserting a bit-zero
/// global imbalance would fail for a correct operator.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/core/units/Literals.hpp>
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
using cfd::CfdDivergenceProblem;
using cfd::CfdDivergenceSummary;
using cfd::CfdGeometry;
using cfd::CfdGradientMethod;
using cfd::CfdMassFluxDivergenceField;
using cfd::CfdMassFluxDivergenceInput;
using cfd::CfdResolvedTarget;
using cfd::CfdTopology;
using cfd::CfdVelocityDivergenceField;
using cfd::CfdVelocityDivergenceInput;
using cfd::FaceIndex;
using cfd::MassFlowRate;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PreparedCfdBoundary;
using cfd::PressureOutlet;
using cfd::SlipWall;
using cfd::VelocityInlet;
using cfd::Velocity3;
using cfd::VolumetricFlowRate;
using meshing::ElementId;
using meshing::NodeId;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

using Vec = std::array<double, 3>;
/// A 3x3, row-major, for a linear velocity field's tensor.
using Mat = std::array<double, 9>;

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// How many units of `eps * valueScale / lengthScale` a divergence may be out
/// by, where the scales are the size of the velocities that cancel and the
/// smallest distance involved.
///
/// THE BOUND IS IN UNITS OF THE PRODUCTS, NOT OF THE ANSWER, which is
/// P18-ELEM-001's finding F4 and the mistake P19-GRAD-001 made first. A surface
/// sum accumulates `|S_f| |U_f|` and then divides by a volume, so the error has
/// nothing to do with how large the divergence turns out to be: a
/// divergence-free field has an answer of zero and a perfectly ordinary
/// absolute error.
///
/// MEASURED, THEN BOUNDED. The worst reading over every case in this file is
/// about one unit. 64 is the factor P19-DIFF-001 and P19-GRAD-001 both settled
/// on for the same kind of bound.
constexpr double kFluxProductUnits = 64.0;

/// The relative error a linear field's divergence may show on a REFERENCE body.
/// The same 1e-9 the thermal suite uses for an assembled mesh, and for the same
/// reason: the bound is asserted over thousands of cells of varying shape.
constexpr double kExactOnAReferenceBody = 1.0e-9;

/// The constant used for constant-field cases.
///
/// NOT A ROUND NUMBER, AND NOT A DYADIC ONE, and P19-GRAD-001 is why. Its sweep
/// found that `0.0` cannot distinguish an unstable interpolation from a stable
/// one (both give exactly zero) and that `17.25` cannot either, because it is
/// `69/4`, so every product is exact and the two forms agree bit-for-bit. A
/// search over 200,000 constants found 16% that discriminate at a weight of
/// 2/3; this is the one that suite settled on, and the unstable form returns it
/// as `12345.678900000003`.
constexpr double kDiscriminatingConstant = 12345.6789;

/// The four faces of a tetrahedron, wound outward for a positively oriented node
/// order. The same table `CfdGradientTests.cpp` and `CfdDiffusionTests.cpp` use.
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
/// all exact. NOT orthogonal, so the interpolation weight is not 1/2 and a
/// mutation of it is visible.
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
// Small vector arithmetic, written out here
// ---------------------------------------------------------------------------

[[nodiscard]] Vec siOf(const Point3D& p) { return {p.x.si(), p.y.si(), p.z.si()}; }
[[nodiscard]] Vec siOf(const AreaVector3D& v) { return {v.x.si(), v.y.si(), v.z.si()}; }
[[nodiscard]] Vec siOf(const Translation3D& v) { return {v.x.si(), v.y.si(), v.z.si()}; }

[[nodiscard]] double dotOf(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
[[nodiscard]] Vec subOf(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
[[nodiscard]] double normOf(const Vec& a) { return std::hypot(a[0], a[1], a[2]); }

[[nodiscard]] Vec applyTo(const Mat& r, const Vec& v) {
    return {r[0] * v[0] + r[1] * v[1] + r[2] * v[2], r[3] * v[0] + r[4] * v[1] + r[5] * v[2],
            r[6] * v[0] + r[7] * v[1] + r[8] * v[2]};
}

[[nodiscard]] double traceOf(const Mat& a) { return a[0] + a[4] + a[8]; }

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

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

    /// Every facet a no-slip wall: the velocity is FixedValue at zero.
    [[nodiscard]] PreparedCfdBoundary walls() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    }

    /// Every facet a pressure outlet: the velocity is ZeroGradient, and `phi` is
    /// COMPUTED, which is the policy the mass-flux path must not refuse.
    [[nodiscard]] PreparedCfdBoundary outlets() const {
        return uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            PressureOutlet{someFace(), 0_Pa}});
    }

    /// Every facet a slip wall: the velocity is Symmetry everywhere.
    [[nodiscard]] PreparedCfdBoundary slips() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
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

void assignWaterTo(Document& document) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> id = features::createMaterial(document, "CfdWater", water);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

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

    [[nodiscard]] PreparedCfdBoundary wallEverywhere() const {
        const CfdResolvedTarget target{
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}},
            facets()};
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

/// One meshable reference body: its kind, and the name to report it by.
struct Meshable {
    bettercad::reference::MeshReferenceModelKind kind{};
    std::string_view name{};
};

/// The reference bodies that P16 meshes. The one model carrying
/// `expectMesh == false` is filtered out and ASSERTED to be the one expected, so
/// a reordering of the table cannot silently drop a different body.
[[nodiscard]] std::vector<Meshable> meshableModels() {
    std::vector<Meshable> out;
    for (const auto& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        out.push_back(Meshable{info.kind, info.name});
    }
    CHECK(out.size() == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(out.size() == 8);
    return out;
}

// ---------------------------------------------------------------------------
// Oracles, written out again
// ---------------------------------------------------------------------------

/// THE INTERPOLATION WEIGHT ORACLE, AND IT IS INDEPENDENT NOW.
///
/// It used to be a TRANSCRIPTION: `orthogonalDistance` and `cellToFace`, read in
/// the same order as production, with the same `(total - owner) / total`. Two
/// independent reviews found it, and a copy cannot reveal a shared
/// misunderstanding of the formula -- only a typo in one of the two copies.
///
/// This form reads NEITHER accessor. From the two CELL CENTROIDS and the FACE
/// CENTROID directly,
///
///     w  =  ((C_N - C_f) . n) / ((C_N - C_P) . n)
///
/// which is the owner's share by the same normal projection assembled out of
/// different accessors -- so a transposition inside `cellToFace` or
/// `orthogonalDistance`, which production reads and this does not, now shows up
/// as a disagreement instead of cancelling.
[[nodiscard]] double weightOracle(const CfdTopology& topology, const CfdGeometry& geometry,
                                  FaceIndex face) {
    const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
    REQUIRE(normal.has_value());
    const Vec n{normal->x(), normal->y(), normal->z()};
    const std::optional<CellIndex> neighbour = topology.neighbour(face);
    REQUIRE(neighbour.has_value());
    const std::optional<Point3D> ownerCentre = geometry.cellCentroid(topology.owner(face));
    const std::optional<Point3D> neighbourCentre = geometry.cellCentroid(*neighbour);
    const std::optional<Point3D> faceCentre = geometry.faceCentroid(face);
    REQUIRE(ownerCentre.has_value());
    REQUIRE(neighbourCentre.has_value());
    REQUIRE(faceCentre.has_value());
    const double toNeighbour = dotOf(subOf(siOf(*neighbourCentre), siOf(*faceCentre)), n);
    const double across = dotOf(subOf(siOf(*neighbourCentre), siOf(*ownerCentre)), n);
    REQUIRE(across != 0.0);
    return toNeighbour / across;
}

/// THE SLIP PROJECTION ORACLE: `U_b = U_P - (U_P . n) n`. The alternative
/// ADR-071 Decision 7 discards, kept here to measure what discarding it bought.
[[nodiscard]] Vec slipOracle(const Vec& velocity, const Vec& unitNormal) {
    const double along = dotOf(velocity, unitNormal);
    return {velocity[0] - unitNormal[0] * along, velocity[1] - unitNormal[1] * along,
            velocity[2] - unitNormal[2] * along};
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

/// A uniform velocity, component-blocked.
[[nodiscard]] std::vector<double> uniformVelocity(std::size_t cells, const Vec& value) {
    std::vector<double> values(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t i = 0; i < 3; ++i) {
            values[i * cells + c] = value[i];
        }
    }
    return values;
}

[[nodiscard]] CfdVelocityDivergenceInput velocityInput(std::span<const double> values) {
    CfdVelocityDivergenceInput input;
    input.currentVelocity = values;
    return input;
}

[[nodiscard]] CfdMassFluxDivergenceInput massFluxInput(std::span<const double> values) {
    CfdMassFluxDivergenceInput input;
    input.faceFlux = values;
    return input;
}

/// THE SIGNED SURFACE SUM ORACLE for a FACE flux: `sum_f ±phi_f` over @p cell's
/// faces, with the sign read from `owner` exactly as ADR-064 requires.
///
/// IT READS `owner`, WHICH IS SHARED WITH PRODUCTION ON PURPOSE. It therefore
/// kills a mutation of the sign and would NOT reveal a shared misunderstanding
/// of the winding convention. `cellSurfaceVectorSum` is the independent
/// authority on that, and the closure case consults it.
[[nodiscard]] double faceFluxSumOracle(const CfdTopology& topology, std::span<const double> faceFlux,
                                       CellIndex cell) {
    double total = 0.0;
    for (const FaceIndex face : topology.cellFaces(cell)) {
        const double flux = faceFlux[face.value()];
        total += topology.owner(face).value() == cell.value() ? flux : -flux;
    }
    return total;
}

/// THE VELOCITY SURFACE SUM ORACLE: `sum_f ±(U_f . S_f)` over @p cell's faces,
/// with the face velocity formed here rather than read from the operator.
[[nodiscard]] double velocityFluxSumOracle(const CfdTopology& topology, const CfdGeometry& geometry,
                                           const PreparedCfdBoundary& boundary,
                                           std::span<const double> velocity, CellIndex cell) {
    const std::size_t cells = topology.cells();
    const auto cellVelocity = [&](std::size_t index) {
        return Vec{velocity[index], velocity[cells + index], velocity[2 * cells + index]};
    };
    double total = 0.0;
    for (const FaceIndex face : topology.cellFaces(cell)) {
        const Vec stored = siOf(geometry.faceAreaVector(face));
        const CellIndex owner = topology.owner(face);
        double flux = 0.0;
        if (topology.isInternal(face)) {
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            REQUIRE(neighbour.has_value());
            const double w = weightOracle(topology, geometry, face);
            const Vec p = cellVelocity(owner.value());
            const Vec n = cellVelocity(neighbour->value());
            const Vec interpolated{n[0] + w * (p[0] - n[0]), n[1] + w * (p[1] - n[1]),
                                   n[2] + w * (p[2] - n[2])};
            flux = dotOf(stored, interpolated);
        } else {
            const std::optional<BoundaryFaceIndex> slot = topology.boundaryFaceOf(face);
            REQUIRE(slot.has_value());
            const std::optional<cfd::CfdBoundaryTreatment> treatment = boundary.treatment(*slot);
            REQUIRE(treatment.has_value());
            if (treatment->velocity == CfdBoundaryPolicy::Symmetry) {
                flux = 0.0;
            } else if (treatment->velocity == CfdBoundaryPolicy::ZeroGradient) {
                flux = dotOf(stored, cellVelocity(owner.value()));
            } else {
                const std::optional<cfd::CfdBoundaryFace> facet = boundary.face(*slot);
                REQUIRE(facet.has_value());
                REQUIRE(facet->velocity.has_value());
                const Vec prescribed{facet->velocity->x.si(), facet->velocity->y.si(),
                                     facet->velocity->z.si()};
                flux = dotOf(stored, prescribed);
            }
        }
        total += owner.value() == cell.value() ? flux : -flux;
    }
    return total;
}

/// The scale a divergence's absolute error belongs to: `eps * |U| * sum|S| / V`.
[[nodiscard]] double divergenceErrorScale(const CfdTopology& topology, const CfdGeometry& geometry,
                                         CellIndex cell, double velocityScale) {
    double area = 0.0;
    for (const FaceIndex face : topology.cellFaces(cell)) {
        area += normOf(siOf(geometry.faceAreaVector(face)));
    }
    const double volume = geometry.cellVolume(cell).si();
    REQUIRE(volume > 0.0);
    return std::numeric_limits<double>::epsilon() * velocityScale * area / volume;
}

} // namespace

// ===========================================================================
// SI units
// ===========================================================================

TEST_CASE("CfdDivergence_TheFluxQuantitiesCarryTheDimensionsTheirNamesClaim", "[cfd][div]") {
    // ADR-066's table says `phi` is mass/time. The alias must agree with the
    // table rather than with a comment about it.
    const std::optional<cfd::CfdFieldDescriptor> phi =
        cfd::descriptorOf(cfd::CfdVariable::MassFlux);
    REQUIRE(phi.has_value());
    CHECK(phi->dimension == dimensions::mass / dimensions::time);
    STATIC_REQUIRE(std::is_same_v<MassFlowRate, Quantity<dimensions::mass / dimensions::time>>);

    STATIC_REQUIRE(std::is_same_v<VolumetricFlowRate, Quantity<dimensions::volume / dimensions::time>>);

    // The derived divergence types, which is what `divergenceAt` returns.
    STATIC_REQUIRE(std::is_same_v<CfdVelocityDivergenceField::Divergence, cfd::VelocityGradient>);
    STATIC_REQUIRE(std::is_same_v<CfdMassFluxDivergenceField::Divergence,
                                  Quantity<dimensions::density / dimensions::time>>);

    // Distinct types, so one field cannot be passed where the other is wanted.
    STATIC_REQUIRE(!std::is_same_v<MassFlowRate, VolumetricFlowRate>);
    STATIC_REQUIRE(!std::is_same_v<MassFlowRate, double>);
}

TEST_CASE("CfdDivergence_EveryProblemHasAName", "[cfd][div]") {
    CHECK(cfd::kCfdDivergenceProblems.size() == 7);
    for (const CfdDivergenceProblem problem : cfd::kCfdDivergenceProblems) {
        const std::string_view name = cfd::toString(problem);
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
    }
    // Every name distinct, so two problems cannot report as one.
    std::vector<std::string_view> names;
    for (const CfdDivergenceProblem problem : cfd::kCfdDivergenceProblems) {
        names.push_back(cfd::toString(problem));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());

    // AND EACH NAME BELONGS TO ITS OWN ENUMERATOR, which distinctness does not
    // establish. A review found that rotating the seven returned strings by one
    // position left every one non-empty, distinct and not "unknown", so the
    // whole suite passed while a non-finite result reported
    // `geometry_describes_another_topology`. These names are the stable external
    // vocabulary of a refusal; nothing else in the repository spells them.
    CHECK(cfd::toString(CfdDivergenceProblem::GeometryDescribesAnotherTopology) ==
          "geometry_describes_another_topology");
    CHECK(cfd::toString(CfdDivergenceProblem::BoundaryDescribesAnotherTopology) ==
          "boundary_describes_another_topology");
    CHECK(cfd::toString(CfdDivergenceProblem::ValueLengthWrong) == "value_length_wrong");
    CHECK(cfd::toString(CfdDivergenceProblem::ValueNotFinite) == "value_not_finite");
    CHECK(cfd::toString(CfdDivergenceProblem::PrescribedValueMissing) == "prescribed_value_missing");
    CHECK(cfd::toString(CfdDivergenceProblem::VelocityPolicyIsAnOutcome) ==
          "velocity_policy_is_an_outcome");
    CHECK(cfd::toString(CfdDivergenceProblem::DivergenceIsNotFinite) == "divergence_is_not_finite");
}

// ===========================================================================
// Face-flux summation, and the scalar flux path
// ===========================================================================

TEST_CASE("CfdDivergence_AMassFluxDivergenceIsTheSignedSumOfItsFaceValues", "[cfd][div]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const std::size_t faces = hand.topology.faces();
    REQUIRE(faces > 0);

    // A phi that is different on every face, so no two faces can be confused.
    std::vector<double> phi(faces, 0.0);
    for (std::size_t f = 0; f < faces; ++f) {
        phi[f] = 1.0 + 0.25 * static_cast<double>(f);
    }

    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    REQUIRE(field->cells() == hand.topology.cells());

    for (std::size_t c = 0; c < field->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<MassFlowRate> got = field->at(cell);
        REQUIRE(got.has_value());
        const double expected = faceFluxSumOracle(hand.topology, phi, cell);
        CHECK_THAT(got->si(), WithinAbs(expected, 1.0e-15));
    }
}

TEST_CASE("CfdDivergence_AMassFluxDivergenceNeedsOneValuePerFace", "[cfd][div]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const std::size_t faces = hand.topology.faces();

    // One too few, and one too many, and the CELL count, which is the plausible
    // wrong answer for a reader who forgot `phi` is a FACE variable.
    for (const std::size_t wrong : {faces - 1, faces + 1, hand.topology.cells()}) {
        if (wrong == faces) {
            continue;
        }
        const std::vector<double> phi(wrong, 1.0);
        const Result<CfdMassFluxDivergenceField> field =
            cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
        REQUIRE_FALSE(field.has_value());
        CHECK_THAT(field.error().message, ContainsSubstring("face-located mass flow rate"));
        CHECK(cfd::cfdMassFluxDivergenceProblem(hand.topology, hand.geometry, massFluxInput(phi)) ==
              CfdDivergenceProblem::ValueLengthWrong);
    }
}

TEST_CASE("CfdDivergence_TheMassFluxPathTakesNoBoundaryAndAcceptsAComputedPolicy", "[cfd][div]") {
    // THE ASYMMETRY ADR-071 DECISION 5 CHOSE, asserted rather than described.
    // `phi` is Computed at a pressure outlet, and that must NOT be a refusal:
    // the caller supplies the face value, and by the time a divergence is taken
    // the solve has produced it.
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::PressureOutlet).massFlux ==
            CfdBoundaryPolicy::Computed);

    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<double> phi(hand.topology.faces(), 2.5);

    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    CHECK(cfd::cfdMassFluxDivergenceProblem(hand.topology, hand.geometry, massFluxInput(phi)) ==
          std::nullopt);

    // And the mass path consults no policy at all, so none of the policy counts
    // is filled.
    CHECK(field->summary().fixedValueFaces == 0);
    CHECK(field->summary().zeroGradientFaces == 0);
    CHECK(field->summary().symmetryFaces == 0);
    // Nor does it interpolate, so there is no skewness to report -- BUT NOT
    // ASSERTED HERE, and a review is why. This fixture has one cell and no
    // internal face, and `skewness` is empty at a boundary face by ADR-065's
    // own contract, so `worstSkewness` is 0.0 on it for EVERY path and every
    // implementation. Hoisting the skewness reduction above the mass path's
    // early return would have survived it. The claim is pinned in
    // `TheSummaryCountsMatchTheTopology`, which runs the mass path over eight
    // reference bodies with thousands of internal faces.
}

TEST_CASE("CfdDivergence_TheStoredQuantityIsTheIntegralAndDivergenceAtDividesByTheVolume",
          "[cfd][div]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < phi.size(); ++f) {
        phi[f] = 3.0 - 0.5 * static_cast<double>(f);
    }
    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());

    for (std::size_t c = 0; c < field->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<MassFlowRate> integral = field->at(cell);
        const std::optional<CfdMassFluxDivergenceField::Divergence> divergence =
            field->divergenceAt(cell);
        REQUIRE(integral.has_value());
        REQUIRE(divergence.has_value());
        const double volume = hand.geometry.cellVolume(cell).si();
        REQUIRE(volume > 0.0);
        // The division is the ONLY difference between the two.
        CHECK_THAT(divergence->si(), WithinRel(integral->si() / volume, 1.0e-15));
        // And the two are genuinely different numbers on this fixture, so the
        // division is not a no-op that a mutation could delete unseen.
        CHECK(std::abs(volume - 1.0) > 0.1);
    }
}

TEST_CASE("CfdDivergence_AFieldDoesNotHoldACellItWasNotBuiltWith", "[cfd][div]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<double> phi(hand.topology.faces(), 1.0);
    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());
    REQUIRE(field->cells() == 1);

    // Past the end, and an invalid index: TOTAL, never undefined behaviour.
    CHECK(field->at(CellIndex::fromValue(1)) == std::nullopt);
    CHECK(field->divergenceAt(CellIndex::fromValue(1)) == std::nullopt);
    CHECK(field->at(CellIndex{}) == std::nullopt);
    CHECK(field->divergenceAt(CellIndex{}) == std::nullopt);
    // And the one it does hold is there.
    CHECK(field->at(CellIndex::fromValue(0)).has_value());
}

// ===========================================================================
// Internal-face cancellation
// ===========================================================================

TEST_CASE("CfdDivergence_AnInternalFacesTwoContributionsSumToExactlyZero", "[cfd][div]") {
    // THE CENTRAL STRUCTURAL CLAIM (ADR-071 Decision 2), pinned on the only
    // fixture where the two contributions can be separated: a two-cell mesh with
    // exactly one internal face.
    //
    // The method: give the internal face a non-zero phi and every BOUNDARY face
    // zero, so each cell's net outflow IS that face's contribution and nothing
    // else. They must then sum to exactly zero -- not to a tolerance.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    REQUIRE(hand.topology.internalFaces() == 1);
    REQUIRE(hand.topology.cells() == 2);

    std::vector<double> phi(hand.topology.faces(), 0.0);
    // ADR-064 numbers internal faces first, so face 0 is the internal one.
    REQUIRE(hand.topology.isInternal(FaceIndex::fromValue(0)));
    phi[0] = kDiscriminatingConstant;

    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());

    const std::optional<MassFlowRate> first = field->at(CellIndex::fromValue(0));
    const std::optional<MassFlowRate> second = field->at(CellIndex::fromValue(1));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    // EXACTLY, not WithinAbs. This is the claim.
    CHECK(first->si() + second->si() == 0.0);
    // And neither is itself zero, so the sum is not trivially zero.
    CHECK(first->si() != 0.0);
    CHECK(second->si() != 0.0);
    // The owner gets the positive sense, because the stored vector is wound out
    // of the owner.
    const CellIndex owner = hand.topology.owner(FaceIndex::fromValue(0));
    const double ownerValue = owner.value() == 0 ? first->si() : second->si();
    CHECK(ownerValue == kDiscriminatingConstant);
}

TEST_CASE("CfdDivergence_TheOwnerSignIsReadAndNotAssumed", "[cfd][div]") {
    // A cell that merely NEIGHBOURS a face must negate the stored vector.
    // `CfdGeometry`'s own header warns that summing a cell's stored vectors
    // without consulting `owner` leaves a residual of ORDER ONE.
    //
    // This case makes that visible: with every face carrying the SAME phi, a
    // correct operator gives the owner-side and neighbour-side cells different
    // totals, and an operator that ignored `owner` would give both the same.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const std::vector<double> phi(hand.topology.faces(), 1.0);

    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());

    const std::optional<MassFlowRate> first = field->at(CellIndex::fromValue(0));
    const std::optional<MassFlowRate> second = field->at(CellIndex::fromValue(1));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    // Each cell has 1 internal + 3 boundary faces. The owner sees +1 from the
    // internal face and the neighbour -1, and both see +1 from each of their
    // three boundary faces: 4 and 2. Written out, not read from the operator.
    const CellIndex owner = hand.topology.owner(FaceIndex::fromValue(0));
    const double ownerTotal = owner.value() == 0 ? first->si() : second->si();
    const double otherTotal = owner.value() == 0 ? second->si() : first->si();
    CHECK(ownerTotal == 4.0);
    CHECK(otherTotal == 2.0);
    // And they differ, which is what an owner-blind operator could not produce.
    CHECK(ownerTotal != otherTotal);
}

// ===========================================================================
// Boundary-face contribution
// ===========================================================================

TEST_CASE("CfdDivergence_AZeroGradientFacetUsesTheOwnersOwnVelocity", "[cfd][div]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const Vec velocity{2.0, -3.0, 0.5};
    const std::vector<double> values = uniformVelocity(hand.topology.cells(), velocity);

    SECTION("every facet ZeroGradient: the total is U . sum_f S_f, which closure zeroes") {
        const PreparedCfdBoundary boundary = hand.outlets();
        REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::PressureOutlet).velocity ==
                CfdBoundaryPolicy::ZeroGradient);
        const Result<CfdVelocityDivergenceField> field =
            cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        CHECK(field->summary().zeroGradientFaces == hand.topology.boundaryFaces());
        CHECK(field->summary().fixedValueFaces == 0);
        CHECK(field->summary().symmetryFaces == 0);

        const std::optional<VolumetricFlowRate> got = field->at(CellIndex::fromValue(0));
        REQUIRE(got.has_value());
        const double expected = velocityFluxSumOracle(hand.topology, hand.geometry, boundary,
                                                      values, CellIndex::fromValue(0));
        CHECK_THAT(got->si(), WithinAbs(expected, 1.0e-18));
    }

    SECTION("ONE facet ZeroGradient: the total is that facet's flux, all three components") {
        // THIS SECTION EXISTS BECAUSE A MUTATION SURVIVED WITHOUT IT, AND COULD
        // NOT HAVE FAILED. Dropping two of the three components of the owner's
        // velocity passed the section above, because for ANY constant velocity
        // the total over a CLOSED cell is `U . sum_f S_f`, and closure makes
        // that zero whatever `U` is. A cell total is structurally blind to which
        // constant was used.
        //
        // With ONE ZeroGradient facet and three no-slip walls prescribing
        // exactly zero, the total IS that facet's own flux, which depends on all
        // three components.
        const std::vector<ElementId> facets = hand.facets();
        REQUIRE(facets.size() == 4);

        // THE SLANTED FACET, FOUND RATHER THAN ASSUMED. Three of this
        // tetrahedron's four facets lie in coordinate planes, so their area
        // vectors have a single non-zero component and each is blind to two of
        // the three velocity components -- boundary face 0 is one of them, and
        // the mutation survived against it for exactly that reason. The facet
        // whose normal has all three components non-zero is the only one that
        // can see a dropped component.
        std::size_t slantedSlot = facets.size();
        for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
            const Vec area = siOf(
                hand.geometry.faceAreaVector(hand.topology.faceOf(BoundaryFaceIndex::fromValue(b))));
            if (std::abs(area[0]) > 1.0e-12 && std::abs(area[1]) > 1.0e-12 &&
                std::abs(area[2]) > 1.0e-12) {
                slantedSlot = b;
                break;
            }
        }
        INFO(std::format("the slanted facet is boundary slot {}", slantedSlot));
        REQUIRE(slantedSlot < hand.topology.boundaryFaces());
        const ElementId slantedFacet = hand.topology.triangleOf(
            BoundaryFaceIndex::fromValue(slantedSlot));

        std::vector<FacetCondition> assignment;
        std::uint64_t next = 1;
        assignment.push_back(FacetCondition{
            slantedFacet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(next),
                                               PressureOutlet{someFace(next), 0_Pa}}});
        for (const ElementId facet : facets) {
            if (facet.value() == slantedFacet.value()) {
                continue;
            }
            ++next;
            assignment.push_back(FacetCondition{
                facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(next),
                                            NoSlipWall{someFace(next)}}});
        }
        REQUIRE(assignment.size() == 4);
        // The outlet fixes the pressure, so no datum is wanted.
        const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, false);

        const Result<CfdVelocityDivergenceField> field =
            cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        CHECK(field->summary().zeroGradientFaces == 1);
        CHECK(field->summary().fixedValueFaces == 3);

        const std::optional<VolumetricFlowRate> got = field->at(CellIndex::fromValue(0));
        REQUIRE(got.has_value());
        const FaceIndex outlet = hand.topology.faceOf(BoundaryFaceIndex::fromValue(slantedSlot));
        const double expected = dotOf(siOf(hand.geometry.faceAreaVector(outlet)), velocity);
        CHECK(got->si() == expected);
        CHECK(expected != 0.0);

        // AND EVERY COMPONENT MATTERS: zeroing any one of them changes the
        // answer by far more than rounding, which is what makes the check above
        // able to see a dropped component.
        for (std::size_t i = 0; i < 3; ++i) {
            Vec partial = velocity;
            partial[i] = 0.0;
            const double without = dotOf(siOf(hand.geometry.faceAreaVector(outlet)), partial);
            INFO(std::format("component {} contributes {:.12e}", i, expected - without));
            CHECK(std::abs(expected - without) > 1.0e-12);
        }
    }
}

TEST_CASE("CfdDivergence_AFixedValueFacetUsesThePrescribedVelocity", "[cfd][div]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<double> values =
        uniformVelocity(hand.topology.cells(), Vec{7.0, -11.0, 13.0});

    SECTION("a no-slip wall prescribes zero, so every flux is exactly zero") {
        const PreparedCfdBoundary boundary = hand.walls();
        REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::NoSlipWall).velocity ==
                CfdBoundaryPolicy::FixedValue);
        const Result<CfdVelocityDivergenceField> field =
            cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
        REQUIRE(field.has_value());
        CHECK(field->summary().fixedValueFaces == hand.topology.boundaryFaces());
        const std::optional<VolumetricFlowRate> got = field->at(CellIndex::fromValue(0));
        REQUIRE(got.has_value());
        // EXACTLY zero: `0 * S` is exactly `0.0` for every component.
        CHECK(got->si() == 0.0);
        CHECK(values[0] != 0.0);
    }

    SECTION("a velocity inlet's PRESCRIBED velocity is used, not the owner's own") {
        // THIS SECTION EXISTS BECAUSE A MUTATION SURVIVED WITHOUT IT, TWICE, and
        // the second failure is the instructive one.
        //
        // Substituting the owner's own velocity for the prescribed one passed
        // the no-slip case above, because a no-slip wall prescribes ZERO and the
        // owner's velocity dotted with a CLOSED cell's surface vectors also
        // comes to zero -- bit-exactly, on this fixture.
        //
        // A MOVING WALL WAS TRIED NEXT AND CANNOT DO THE JOB EITHER.
        // `bindCfdBoundary` validates that a wall velocity is TANGENTIAL ("the
        // wall does not move" otherwise), and a tangential velocity has no
        // normal component, so its flux through its own facet is zero BY
        // CONSTRUCTION -- indistinguishable from the prescribed zero again. It
        // is also planar-only, which a tetrahedron's four facets are not.
        //
        // A velocity inlet prescribes `-v n_hat`, which is NORMAL to the facet,
        // so its flux is `-v |S_f|`: as far from zero as any condition in
        // ADR-067's table gets. One inlet facet, three no-slip walls that
        // prescribe exactly zero, and the total IS the inlet's own flux.
        const std::vector<ElementId> facets = hand.facets();
        REQUIRE(facets.size() == 4);
        std::vector<FacetCondition> assignment;
        assignment.push_back(FacetCondition{
            facets[0],
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                 VelocityInlet{someFace(1), Velocity::fromSi(3.25)}}});
        for (std::size_t i = 1; i < facets.size(); ++i) {
            assignment.push_back(FacetCondition{
                facets[i], CfdBoundaryCondition{
                               CfdBoundaryConditionId::fromValue(static_cast<std::uint64_t>(i + 1)),
                               NoSlipWall{someFace(static_cast<std::uint64_t>(i + 1))}}});
        }
        // A DATUM IS REQUIRED: the inlet and the walls all give the pressure
        // ZeroGradient, so nothing fixes it (ADR-067 Decision 7).
        const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, true);
        REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::VelocityInlet).velocity ==
                CfdBoundaryPolicy::FixedValue);

        const Result<CfdVelocityDivergenceField> field =
            cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        CHECK(field->summary().fixedValueFaces == 4);

        const std::optional<VolumetricFlowRate> got = field->at(CellIndex::fromValue(0));
        REQUIRE(got.has_value());

        // The prescribed velocity is read from the PREPARED BOUNDARY -- this
        // operator's own input -- rather than re-derived here.
        const FaceIndex inletFace = hand.topology.faceOf(BoundaryFaceIndex::fromValue(0));
        REQUIRE(hand.topology.triangleOf(BoundaryFaceIndex::fromValue(0)).value() ==
                facets[0].value());
        const std::optional<cfd::CfdBoundaryFace> facet =
            boundary.face(BoundaryFaceIndex::fromValue(0));
        REQUIRE(facet.has_value());
        REQUIRE(facet->velocity.has_value());
        const Vec prescribed{facet->velocity->x.si(), facet->velocity->y.si(),
                             facet->velocity->z.si()};
        const double expected = dotOf(siOf(hand.geometry.faceAreaVector(inletFace)), prescribed);
        CHECK(got->si() == expected);
        // An inlet draws fluid IN, so the outward flux is negative and far from
        // zero -- which is exactly what a tangential wall velocity could not be.
        CHECK(expected < -1.0e-9);

        // And the owner's-own velocity through that facet is a DIFFERENT number,
        // which is what makes the check above non-vacuous.
        const double ownerOwn = dotOf(siOf(hand.geometry.faceAreaVector(inletFace)),
                                      Vec{values[0], values[1], values[2]});
        INFO(std::format("prescribed {:.12e} vs owner's own {:.12e}", expected, ownerOwn));
        CHECK(std::abs(expected - ownerOwn) > 1.0e-12);
    }
}

TEST_CASE("CfdDivergence_ASymmetryFacetContributesExactlyZeroFlux", "[cfd][div]") {
    // ADR-071 DECISION 7. A slip wall's normal velocity is zero BY DEFINITION, so
    // the operator writes exactly 0.0 rather than forming `U_P - (U_P . n) n` and
    // dotting it.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.slips();
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::Slip).velocity ==
            CfdBoundaryPolicy::Symmetry);

    const std::vector<double> values =
        uniformVelocity(hand.topology.cells(), Vec{kDiscriminatingConstant, -2.0, 3.0});
    const Result<CfdVelocityDivergenceField> field =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    REQUIRE(field.has_value());

    CHECK(field->summary().symmetryFaces == hand.topology.boundaryFaces());
    const std::optional<VolumetricFlowRate> got = field->at(CellIndex::fromValue(0));
    REQUIRE(got.has_value());
    // Every face contributes exactly zero, so the sum is exactly zero.
    CHECK(got->si() == 0.0);
    CHECK(field->netBoundaryFlux().si() == 0.0);
}

TEST_CASE("CfdDivergence_TheSymmetryExactZeroBeatsTheProjectedAlternative", "[cfd][div]") {
    // THE DISCARDED ALTERNATIVE, MEASURED. ADR-071 Decision 7 claims that forming
    // `U_b = U_P - (U_P . n) n` and dotting it with `S_f` reaches zero only to
    // rounding, because `U_b . n` vanishes exactly only when `n . n` is exactly
    // 1. This case measures that residual, so the decision rests on a number.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const Vec velocity{kDiscriminatingConstant, -2.0, 3.0};

    double worstProjected = 0.0;
    double scale = 0.0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        const Vec stored = siOf(hand.geometry.faceAreaVector(face));
        const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
        REQUIRE(normal.has_value());
        const Vec unitNormal{normal->x(), normal->y(), normal->z()};
        const Vec projected = slipOracle(velocity, unitNormal);
        worstProjected = std::max(worstProjected, std::abs(dotOf(stored, projected)));
        scale = std::max(scale, normOf(stored) * normOf(velocity));
    }

    // The projected form is NOT exactly zero on at least one face -- which is the
    // whole point of the decision. Recorded as a measurement.
    INFO(std::format("the projected alternative leaves {:.6e}, which is {:.3f} eps of {:.6e}",
                     worstProjected, worstProjected / (std::numeric_limits<double>::epsilon() * scale),
                     scale));
    CHECK(worstProjected <= 8.0 * std::numeric_limits<double>::epsilon() * scale);
    // AND IT IS NOT ZERO, WHICH IS THE ACTUAL CLAIM. A review found this case
    // asserting only the upper bound, so it would have passed if the projected
    // form were exactly zero too -- and then substituting the projection into
    // production for `Symmetry` would have passed the exact-zero assertions
    // below as well, because three of this tet's four facets lie in coordinate
    // planes where the residual is exactly zero by construction. Only the
    // slanted facet can be non-zero, and this is the one place in the file where
    // the LOWER bound is the claim.
    CHECK(worstProjected != 0.0);

    // And the operator's own answer is exactly zero, which the alternative is
    // not. If this ever reads 0.0 for the projection too, the decision's
    // justification would be worth revisiting rather than the test relaxing.
    const PreparedCfdBoundary boundary = hand.slips();
    const std::vector<double> values = uniformVelocity(hand.topology.cells(), velocity);
    const Result<CfdVelocityDivergenceField> field =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    REQUIRE(field.has_value());
    CHECK(field->at(CellIndex::fromValue(0))->si() == 0.0);
}

TEST_CASE("CfdDivergence_TheBoundaryPolicyCountsSumToTheBoundaryFaceCount", "[cfd][div]") {
    // A MIXED boundary, so the three counters are filled from three different
    // policies and a swap between them is visible.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<ElementId> facets = hand.facets();
    REQUIRE(facets.size() == 4);

    const std::array<FacetCondition, 4> assignment{
        FacetCondition{facets[0],
                       CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            NoSlipWall{someFace(1)}}},
        FacetCondition{facets[1], CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2),
                                                        SlipWall{someFace(2)}}},
        FacetCondition{facets[2],
                       CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3),
                                            PressureOutlet{someFace(3), 0_Pa}}},
        FacetCondition{facets[3], CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(4),
                                                        NoSlipWall{someFace(4)}}}};
    const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, false);

    const std::vector<double> values = uniformVelocity(hand.topology.cells(), Vec{1.0, 2.0, 3.0});
    const Result<CfdVelocityDivergenceField> field =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    const CfdDivergenceSummary& summary = field->summary();
    CHECK(summary.fixedValueFaces + summary.zeroGradientFaces + summary.symmetryFaces ==
          hand.topology.boundaryFaces());
    // And each is individually what the table says, so a swap is caught.
    CHECK(summary.fixedValueFaces == 2);
    CHECK(summary.symmetryFaces == 1);
    CHECK(summary.zeroGradientFaces == 1);
    CHECK(summary.boundaryFaces == 4);
    CHECK(summary.internalFaces == 0);
}

// ===========================================================================
// Constant-field validation
// ===========================================================================

TEST_CASE("CfdDivergence_AConstantVelocityDivergesToZeroOnAFixture", "[cfd][div]") {
    // A constant field is divergence-free. With ZeroGradient everywhere the face
    // value is the constant at every face, so the sum is `c . sum_f S_f`, which
    // closure makes zero to rounding.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    const Vec constant{kDiscriminatingConstant, -2.0 * kDiscriminatingConstant, 0.75};
    const std::vector<double> values = uniformVelocity(hand.topology.cells(), constant);

    const Result<CfdVelocityDivergenceField> field =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    for (std::size_t c = 0; c < field->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<CfdVelocityDivergenceField::Divergence> divergence =
            field->divergenceAt(cell);
        REQUIRE(divergence.has_value());
        const double bound =
            kFluxProductUnits * divergenceErrorScale(hand.topology, hand.geometry, cell, normOf(constant));
        INFO(std::format("cell {} divergence {:.6e}, bound {:.6e}", c, divergence->si(), bound));
        CHECK(std::abs(divergence->si()) <= bound);

        // AND EXACTLY AGAINST THE ORACLE, which is what detects an unstable
        // interpolation. THE BOUND ABOVE CANNOT: `w U_P + (1-w) U_N` differs
        // from `U_N + w (U_P - U_N)` by about one unit of
        // `eps * |U| * sum|S| / V` for a constant field, and the bound allows
        // sixty-four. A mutation to the unstable form survived this case until
        // this assertion was added -- the same defect P19-GRAD-001's sweep found
        // in its own constant-field test, arriving here by a different route.
        const std::optional<VolumetricFlowRate> integral = field->at(cell);
        REQUIRE(integral.has_value());
        const double oracle =
            velocityFluxSumOracle(hand.topology, hand.geometry, boundary, values, cell);
        CHECK(integral->si() == oracle);
    }
}

TEST_CASE("CfdDivergence_TheWorstClosureIsReportedAndBoundsTheConstantModeResidual", "[cfd][div]") {
    // `worstClosure` must be a MEASUREMENT of the mesh, not a constant. It is
    // checked against `cellClosure` read independently, and it must be the
    // maximum rather than the last or the first.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const std::vector<double> phi(hand.topology.faces(), 1.0);
    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());

    double expected = 0.0;
    std::vector<double> each;
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        const std::optional<double> closure = hand.geometry.cellClosure(CellIndex::fromValue(c));
        REQUIRE(closure.has_value());
        each.push_back(*closure);
        expected = std::max(expected, *closure);
    }
    CHECK(field->summary().worstClosure == expected);
    CHECK(each.size() == 2);
    INFO(std::format("closures {:.3e} and {:.3e}", each[0], each[1]));

    // THE INDEPENDENT AUTHORITY ON THE WINDING, consulted rather than only
    // cited. This file's header has claimed since it was written that
    // `cellSurfaceVectorSum` is where the convention is checked independently of
    // `topology.owner`, and a review found the accessor was never called. It is
    // ADR-065's own owner-signed sum, so for every cell it must agree with the
    // sum this test forms from `faceAreaVector` and `owner` -- the one place the
    // suite's reading of ADR-064 is checked against the geometry module's.
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<AreaVector3D> authority = hand.geometry.cellSurfaceVectorSum(cell);
        REQUIRE(authority.has_value());
        Vec mine{0.0, 0.0, 0.0};
        for (const FaceIndex face : hand.topology.cellFaces(cell)) {
            const Vec stored = siOf(hand.geometry.faceAreaVector(face));
            const double sign = hand.topology.owner(face).value() == c ? 1.0 : -1.0;
            mine = {mine[0] + sign * stored[0], mine[1] + sign * stored[1],
                    mine[2] + sign * stored[2]};
        }
        const Vec theirs = siOf(*authority);
        INFO(std::format("cell {}: mine ({:.3e},{:.3e},{:.3e}) theirs ({:.3e},{:.3e},{:.3e})", c,
                         mine[0], mine[1], mine[2], theirs[0], theirs[1], theirs[2]));
        CHECK_THAT(mine[0], WithinAbs(theirs[0], 1.0e-18));
        CHECK_THAT(mine[1], WithinAbs(theirs[1], 1.0e-18));
        CHECK_THAT(mine[2], WithinAbs(theirs[2], 1.0e-18));
    }
    // Closure is at rounding on this fixture, which is the claim ADR-071
    // Decision 8 rests on.
    CHECK(expected < 1.0e-14);

    // THE REDUCTION IS PINNED ON A REFERENCE BODY, NOT HERE, and a mutation is
    // why. Two cells whose closures happen to be EQUAL make a maximum and a
    // plain assignment indistinguishable, and that is exactly what happened:
    // `max` replaced by `=` survived this case. On a body with thousands of
    // cells the values genuinely differ, so the reduction has something to
    // reduce and the last cell is not the largest.
    const Reference reference{bettercad::reference::MeshReferenceModelKind::Cylinder};
    const std::vector<double> many(reference.topology->faces(), 1.0);
    const Result<CfdMassFluxDivergenceField> big =
        cfd::divergeCfdMassFlux(*reference.topology, *reference.geometry, massFluxInput(many));
    REQUIRE(big.has_value());

    double worst = 0.0;
    double last = 0.0;
    std::size_t strictlySmaller = 0;
    const std::size_t cells = reference.topology->cells();
    REQUIRE(cells > 100);
    for (std::size_t c = 0; c < cells; ++c) {
        const std::optional<double> closure = reference.geometry->cellClosure(CellIndex::fromValue(c));
        REQUIRE(closure.has_value());
        worst = std::max(worst, *closure);
        last = *closure;
    }
    for (std::size_t c = 0; c < cells; ++c) {
        const std::optional<double> closure = reference.geometry->cellClosure(CellIndex::fromValue(c));
        if (*closure < worst) {
            ++strictlySmaller;
        }
    }
    INFO(std::format("{} cells, worst closure {:.6e}, last cell {:.6e}, {} strictly smaller", cells,
                     worst, last, strictlySmaller));
    // The reported figure is the MAXIMUM.
    CHECK(big->summary().worstClosure == worst);
    // Most cells are strictly below it, so a reduction that kept only the last
    // value would have to be lucky to agree -- and the last value is checked
    // against the maximum directly.
    CHECK(strictlySmaller > cells / 2);
    CHECK(last < worst);
}

TEST_CASE("CfdDivergence_AConstantVelocityDivergesToZeroOnEveryReferenceBody", "[cfd][div]") {
    const Vec constant{kDiscriminatingConstant, -1.5, 2.25};
    for (const Meshable& body : meshableModels()) {
        const Reference reference{body.kind};
        const PreparedCfdBoundary boundary = reference.wallEverywhere();
        // A no-slip wall prescribes zero velocity, which does NOT agree with a
        // non-zero interior constant -- so the field described is not uniform.
        // The ZeroGradient reading is the one that tests the constant mode, and a
        // wall-everywhere mesh cannot provide it. What IS asserted here is that
        // the prescribed-zero boundary makes every boundary flux exactly zero, so
        // the whole divergence comes from the internal faces, and a constant
        // field makes each of those cancel against its neighbour.
        const std::vector<double> values =
            uniformVelocity(reference.topology->cells(), constant);
        const Result<CfdVelocityDivergenceField> field = cfd::divergeCfdVelocity(
            *reference.topology, *reference.geometry, boundary, velocityInput(values));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        // Every boundary face prescribes zero, so the whole mesh exchanges
        // nothing with the outside: EXACTLY zero.
        CHECK(field->netBoundaryFlux().si() == 0.0);
        // And the global sum is then the accumulated rounding of internal faces
        // that each cancel exactly, so it is at rounding rather than bit-zero.
        INFO(std::format("{}: relative imbalance {:.3e} over {} cells",
                         body.name, field->summary().relativeImbalance,
                         field->cells()));
        // THE BOUND, AND ITS REASON. `relativeImbalance` is already
        // dimensionless -- an imbalance divided by the magnitude the sum
        // accumulated -- so this is the one figure in the file that is
        // legitimately compared against a bare number. Experiment 01 Q2b
        // measured the worst over 20,000 random meshes at 1.125e-16, about half
        // an eps; a reference body accumulates over thousands of faces rather
        // than seven, so the bound allows roughly 450 eps of growth. It is
        // stated as a number this loose deliberately: tightening it to the
        // measured value would make a correct operator fail on a mesh with more
        // cells, which is exactly what ADR-071's withdrawn bit-exactness
        // prediction would have done.
        CHECK(field->summary().relativeImbalance < 1.0e-13);
    }
}

// ===========================================================================
// Linear-field validation, and the independent analytic check
// ===========================================================================

TEST_CASE("CfdDivergence_ALinearVelocityDivergesToItsTraceOnAFixture", "[cfd][div]") {
    // `u = G x + c` has `div u = trace(G)` identically. With ZeroGradient
    // boundaries the face value on a boundary face is the owner's own, which is
    // NOT the true value at the face centroid -- so the exactness that ADR-071
    // Q4 measured needs the TRUE face value, and this fixture cannot supply it
    // on its boundary faces. What is asserted is the internal face's share:
    // the two-cell pair's cells both see the same interpolation.
    //
    // The honest, strong statement available on a hand fixture is the
    // ORACLE AGREEMENT, which pins every branch of the face-value logic, plus the
    // trace on a mesh whose boundary values ARE the true ones -- which is the
    // reference-body case below.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.outlets();
    const Mat tensor{0.5, 0.25, -0.125, 0.0, -1.5, 0.75, 0.25, 0.5, 2.0};
    const Vec constant{1.0, -2.0, 3.0};
    const std::vector<double> values = linearVelocity(hand.geometry, tensor, constant);

    const Result<CfdVelocityDivergenceField> field =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    for (std::size_t c = 0; c < field->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<VolumetricFlowRate> got = field->at(cell);
        REQUIRE(got.has_value());
        const double expected =
            velocityFluxSumOracle(hand.topology, hand.geometry, boundary, values, cell);
        INFO(std::format("cell {}: operator {:.12e}, oracle {:.12e}", c, got->si(), expected));
        CHECK_THAT(got->si(), WithinRel(expected, 1.0e-14));
    }

    // And the trace is what the field's divergence approaches: recorded as a
    // measurement rather than asserted tightly, because the boundary face values
    // are the owner's own and not the true ones.
    INFO(std::format("trace(G) = {:.6f}", traceOf(tensor)));
    CHECK(std::isfinite(traceOf(tensor)));
}

TEST_CASE("CfdDivergence_ALinearVelocityDivergesToItsTraceWithTrueFaceValues", "[cfd][div]") {
    // THE IDENTITY ADR-071 Q4 MEASURED, on real geometry rather than random
    // tetrahedra: with the TRUE face value the surface sum is linear-exact, so
    // the divergence equals `trace(G)`.
    //
    // The operator cannot be handed true face values through its velocity entry
    // point, because it interpolates. So the identity is checked through the MASS
    // FLUX path, which takes face values directly -- supplying `U(C_f) . S_f` per
    // face is exactly the true-face-value surface sum, and the path that sums it
    // is the same scatter.
    const Mat tensor{0.5, 0.25, -0.125, 0.0, -1.5, 0.75, 0.25, 0.5, 2.0};
    const Vec constant{1.0, -2.0, 3.0};
    const double trace = traceOf(tensor);

    for (const auto& points : {orthogonalPoints(), analyticPoints()}) {
        const auto& tets = points.size() == 5 && points[4].y.si() > 0.5 ? kAnalyticPair : kTwoCells;
        const Hand hand = Hand::make(points, tets);

        std::vector<double> faceFlux(hand.topology.faces(), 0.0);
        for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const std::optional<Point3D> centroid = hand.geometry.faceCentroid(face);
            REQUIRE(centroid.has_value());
            const Vec at = applyTo(tensor, siOf(*centroid));
            const Vec trueValue{at[0] + constant[0], at[1] + constant[1], at[2] + constant[2]};
            faceFlux[f] = dotOf(siOf(hand.geometry.faceAreaVector(face)), trueValue);
        }

        const Result<CfdMassFluxDivergenceField> field =
            cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(faceFlux));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        for (std::size_t c = 0; c < field->cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            const std::optional<CfdMassFluxDivergenceField::Divergence> divergence =
                field->divergenceAt(cell);
            REQUIRE(divergence.has_value());
            const double bound = kFluxProductUnits *
                                 divergenceErrorScale(hand.topology, hand.geometry, cell,
                                                      normOf(constant) + normOf(Vec{1.0, 4.0, 2.0}));
            INFO(std::format("cell {}: got {:.12e}, trace {:.12e}, bound {:.3e}", c, divergence->si(),
                             trace, bound));
            CHECK(std::abs(divergence->si() - trace) <= bound);
        }
    }
}

TEST_CASE("CfdDivergence_ALinearVelocityDivergesToItsTraceOnEveryReferenceBody", "[cfd][div]") {
    const Mat tensor{0.25, 0.0, 0.0, 0.0, -0.75, 0.0, 0.0, 0.0, 1.25};
    const Vec constant{0.5, -0.25, 0.125};
    const double trace = traceOf(tensor);

    for (const Meshable& body : meshableModels()) {
        const Reference reference{body.kind};
        std::vector<double> faceFlux(reference.topology->faces(), 0.0);
        for (std::size_t f = 0; f < reference.topology->faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const std::optional<Point3D> centroid = reference.geometry->faceCentroid(face);
            REQUIRE(centroid.has_value());
            const Vec at = applyTo(tensor, siOf(*centroid));
            const Vec trueValue{at[0] + constant[0], at[1] + constant[1], at[2] + constant[2]};
            faceFlux[f] = dotOf(siOf(reference.geometry->faceAreaVector(face)), trueValue);
        }

        const Result<CfdMassFluxDivergenceField> field = cfd::divergeCfdMassFlux(
            *reference.topology, *reference.geometry, massFluxInput(faceFlux));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        // THE BOUND IS IN UNITS OF THE PRODUCTS, NOT OF THE ANSWER, and that is
        // a correction two independent reviews both asked for. This read
        // `worst <= kExactOnAReferenceBody * max(|trace|, 1.0)` -- an
        // ANSWER-relative bound, in the file whose own `kFluxProductUnits` doc
        // states the rule against them in capitals, and the exact error
        // (P19-GRAD-001's r8) this milestone's first experiment had already made
        // once in its own denominator. `divergenceErrorScale` was two cases
        // away, and the companion case on a hand fixture was already using it.
        double worst = 0.0;
        double worstRatio = 0.0;
        for (std::size_t c = 0; c < field->cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            const std::optional<CfdMassFluxDivergenceField::Divergence> divergence =
                field->divergenceAt(cell);
            REQUIRE(divergence.has_value());
            const double error = std::abs(divergence->si() - trace);
            const double bound =
                kFluxProductUnits * divergenceErrorScale(*reference.topology, *reference.geometry,
                                                         cell, normOf(constant) +
                                                                   normOf(Vec{0.25, 0.75, 1.25}));
            worst = std::max(worst, error);
            worstRatio = std::max(worstRatio, bound > 0.0 ? error / bound : 0.0);
            INFO(std::format("{} cell {}: got {:.12e}, trace {:.12e}, bound {:.3e}", body.name, c,
                             divergence->si(), trace, bound));
            CHECK(error <= bound);
        }
        INFO(std::format("{}: worst |div - trace| = {:.6e} over {} cells, worst error/bound {:.3f}",
                         body.name, worst, field->cells(), worstRatio));
    }
}

TEST_CASE("CfdDivergence_AnAnalyticOneCellTetMatchesClosedFormArithmetic", "[cfd][div]") {
    // The one-cell tet on (0,0,0), (10,0,0), (0,10,0), (0,0,10) mm. With a face
    // flux of exactly 1 on each of its four faces and the owner being cell 0 for
    // all of them, the net outflow is EXACTLY 4.
    //
    // And the volume is `(10mm)^3 / 6 = 1.6666...e-7 m^3`, so the divergence is
    // `4 / (1e-6/6) = 2.4e7` exactly in rational arithmetic.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.cells() == 1);
    REQUIRE(hand.topology.faces() == 4);
    REQUIRE(hand.topology.internalFaces() == 0);

    const std::vector<double> phi(4, 1.0);
    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());

    const std::optional<MassFlowRate> integral = field->at(CellIndex::fromValue(0));
    REQUIRE(integral.has_value());
    CHECK(integral->si() == 4.0);

    // The volume, from closed-form arithmetic rather than from the accessor.
    const double expectedVolume = 1.0e-6 / 6.0;
    CHECK_THAT(hand.geometry.cellVolume(CellIndex::fromValue(0)).si(),
               WithinRel(expectedVolume, 1.0e-15));

    const std::optional<CfdMassFluxDivergenceField::Divergence> divergence =
        field->divergenceAt(CellIndex::fromValue(0));
    REQUIRE(divergence.has_value());
    CHECK_THAT(divergence->si(), WithinRel(4.0 / expectedVolume, 1.0e-14));

    // Every face is a boundary face, so the boundary flux IS the whole answer.
    CHECK(field->netBoundaryFlux().si() == 4.0);
    CHECK(field->globalNetFlux().si() == 4.0);
    CHECK(field->globalImbalance().si() == 0.0);
}

// ===========================================================================
// Closed-volume conservation
// ===========================================================================

TEST_CASE("CfdDivergence_TheGlobalNetFluxEqualsTheBoundaryFluxToRounding", "[cfd][div]") {
    // ADR-071 DECISION 3, asserted at the strength the measurement supports and
    // NO STRONGER. The two are equal in exact arithmetic; in floating point the
    // imbalance is at rounding, so it is bounded relative to the accumulated
    // magnitude rather than required to be zero.
    for (const Meshable& body : meshableModels()) {
        const Reference reference{body.kind};
        std::vector<double> phi(reference.topology->faces(), 0.0);
        for (std::size_t f = 0; f < phi.size(); ++f) {
            phi[f] = std::sin(0.5 * static_cast<double>(f)) * 3.0 + 1.0;
        }
        const Result<CfdMassFluxDivergenceField> field =
            cfd::divergeCfdMassFlux(*reference.topology, *reference.geometry, massFluxInput(phi));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        const double imbalance = field->globalImbalance().si();
        INFO(std::format("{}: imbalance {:.6e}, relative {:.6e}, cells {}",
                         body.name, imbalance,
                         field->summary().relativeImbalance, field->cells()));
        // The reported relative figure is the one to judge.
        CHECK(field->summary().relativeImbalance < 1.0e-13);
        // And it agrees with the signed difference of the two accessors.
        CHECK_THAT(imbalance,
                   WithinAbs(field->globalNetFlux().si() - field->netBoundaryFlux().si(), 0.0));
    }
}

TEST_CASE("CfdDivergence_TheGlobalNetFluxIsTheSumOfEveryCellsNetOutflow", "[cfd][div]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < phi.size(); ++f) {
        phi[f] = 2.0 + static_cast<double>(f);
    }
    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());

    double expected = 0.0;
    for (std::size_t c = 0; c < field->cells(); ++c) {
        expected += field->at(CellIndex::fromValue(c))->si();
    }
    CHECK(field->globalNetFlux().si() == expected);

    // And the boundary total, summed here from the boundary faces alone.
    double boundary = 0.0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        boundary += phi[hand.topology.faceOf(BoundaryFaceIndex::fromValue(b)).value()];
    }
    CHECK_THAT(field->netBoundaryFlux().si(), WithinAbs(boundary, 1.0e-15));
}

TEST_CASE("CfdDivergence_TheRelativeImbalanceIsMeasuredAgainstTheAccumulatedMagnitude",
          "[cfd][div]") {
    // The denominator must be the accumulated magnitude and NOT the answer. The
    // way to see the difference: a flux field whose boundary total nearly
    // cancels. Stated against the answer the figure explodes -- which is exactly
    // the 9.992e+284 this milestone's first experiment reported.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    std::vector<double> phi(hand.topology.faces(), 0.0);
    // Boundary faces come after the internal ones, so make them cancel.
    REQUIRE(hand.topology.internalFaces() == 1);
    REQUIRE(hand.topology.boundaryFaces() == 6);
    phi[0] = 1.0;
    for (std::size_t b = 0; b < 6; ++b) {
        const std::size_t f = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b)).value();
        phi[f] = b % 2 == 0 ? 1.0e6 : -1.0e6;
    }
    // A TINY BUT NON-ZERO RESIDUE, and the "non-zero" is the point. With an
    // EXACT cancellation the answer-relative form short-circuits on its own
    // `denominator > 0` guard and reports zero, which is how a mutation to that
    // form survived this case. A small residue leaves the denominator small and
    // non-zero, so the wrong form explodes and the right one does not.
    {
        const std::size_t f = hand.topology.faceOf(BoundaryFaceIndex::fromValue(0)).value();
        phi[f] = 1.0e6 + 1.0e-9;
    }

    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());

    const double boundary = field->netBoundaryFlux().si();
    INFO(std::format("boundary total {:.6e}, imbalance {:.6e}, relative {:.6e}", boundary,
                     field->globalImbalance().si(), field->summary().relativeImbalance));
    // The boundary total is tiny compared with what was accumulated, which is
    // the condition that breaks an answer-relative figure -- and NON-ZERO, so
    // that form cannot escape through its own guard.
    CHECK(std::abs(boundary) < 1.0e-3);
    CHECK(boundary != 0.0);
    // The reported figure stays sane, because its denominator is the accumulated
    // magnitude.
    CHECK(field->summary().relativeImbalance < 1.0e-14);

    // THE RATIO IDENTITY, ASSERTED EXACTLY, and this is what detects a changed
    // denominator. Engineering a fixture whose boundary total is small was NOT
    // enough: on a two-cell mesh with values like these every addition is
    // exactly representable, so the imbalance is exactly ZERO and BOTH
    // denominators give zero. A mutation to the answer-relative form survived
    // that. Asserting the identity needs no pathological fixture at all.
    //
    // The denominator is `sum_f |phi_f|`, written out here rather than read from
    // the operator.
    const auto ratioIdentityHolds = [](const CfdMassFluxDivergenceField& f,
                                       std::span<const double> flux) {
        double accumulated = 0.0;
        for (const double value : flux) {
            accumulated += std::abs(value);
        }
        const double expected =
            accumulated > 0.0
                ? std::abs(f.globalNetFlux().si() - f.netBoundaryFlux().si()) / accumulated
                : 0.0;
        INFO(std::format("accumulated {:.6e}, expected ratio {:.6e}, reported {:.6e}", accumulated,
                         expected, f.summary().relativeImbalance));
        CHECK(f.summary().relativeImbalance == expected);
    };
    ratioIdentityHolds(*field, phi);

    // AND ON A REFERENCE BODY, where thousands of faces guarantee the imbalance
    // is NOT zero -- so the two candidate denominators genuinely differ and the
    // identity above is non-vacuous.
    const Reference reference{bettercad::reference::MeshReferenceModelKind::Cylinder};
    std::vector<double> many(reference.topology->faces(), 0.0);
    for (std::size_t f = 0; f < many.size(); ++f) {
        many[f] = std::sin(0.37 * static_cast<double>(f)) * 1.0e4 + 7.0 / 3.0;
    }
    const Result<CfdMassFluxDivergenceField> big =
        cfd::divergeCfdMassFlux(*reference.topology, *reference.geometry, massFluxInput(many));
    INFO((big.has_value() ? std::string{} : big.error().message));
    REQUIRE(big.has_value());
    ratioIdentityHolds(*big, many);
    INFO(std::format("reference imbalance {:.6e}, boundary {:.6e}, relative {:.6e}",
                     big->globalImbalance().si(), big->netBoundaryFlux().si(),
                     big->summary().relativeImbalance));
    // Non-zero, so the denominator is load-bearing.
    CHECK(big->globalImbalance().si() != 0.0);
    CHECK(big->summary().relativeImbalance > 0.0);
}

TEST_CASE("CfdDivergence_TheRelativeImbalanceIsZeroWhenNothingWasAccumulated", "[cfd][div]") {
    // A zero flux field accumulates nothing, so the denominator is zero and the
    // reported figure must be zero rather than NaN.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const std::vector<double> phi(hand.topology.faces(), 0.0);
    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE(field.has_value());
    CHECK(field->summary().relativeImbalance == 0.0);
    CHECK_FALSE(std::isnan(field->summary().relativeImbalance));
    CHECK(field->largestMagnitude().si() == 0.0);
}

// ===========================================================================
// Invariance and determinism
// ===========================================================================

TEST_CASE("CfdDivergence_TheDivergenceIsInvariantUnderARigidMotion", "[cfd][div]") {
    // A divergence is a SCALAR, so a rigid motion of the mesh and the field must
    // leave it unchanged. This is the covariance statement for this operator, and
    // it is stronger than the gradient's in one way: there is no tensor law to
    // get right, so a transposition would show up as a changed number rather
    // than as a plausible one.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    const Mat tensor{0.5, 0.25, -0.125, 0.0, -1.5, 0.75, 0.25, 0.5, 2.0};
    const Vec constant{1.0, -2.0, 3.0};
    const std::vector<double> values = linearVelocity(hand.geometry, tensor, constant);
    const Result<CfdVelocityDivergenceField> before =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    REQUIRE(before.has_value());

    // The same mesh moved, with the SAME connectivity index for index -- P16 does
    // not promise the same tetrahedralisation in a moved frame, so the transform
    // is applied node by node.
    // A rotation about Z by 0.7 rad, then a translation. The velocity is a
    // VECTOR, so only the rotation acts on it.
    const RigidTransform3D motion =
        RigidTransform3D::translation(Translation3D{3_mm, -4_mm, 5_mm})
            .after(RigidTransform3D::rotation(
                Axis3D{Point3D{}, *Direction3D::fromComponents(0.0, 0.0, 1.0)},
                Angle::fromSi(0.7)));
    std::vector<Point3D> moved;
    for (const Point3D& p : orthogonalPoints()) {
        moved.push_back(motion.apply(p));
    }
    const Hand after = Hand::make(moved, kTwoCells);
    const PreparedCfdBoundary movedBoundary = after.outlets();

    // The field moved with it: `u'(x') = R u(x)`.
    const Mat r{std::cos(0.7), -std::sin(0.7), 0.0, std::sin(0.7), std::cos(0.7), 0.0, 0.0, 0.0, 1.0};
    const std::size_t cells = after.topology.cells();
    std::vector<double> movedValues(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        const Vec original{values[c], values[cells + c], values[2 * cells + c]};
        const Vec rotated = applyTo(r, original);
        for (std::size_t i = 0; i < 3; ++i) {
            movedValues[i * cells + c] = rotated[i];
        }
    }
    const Result<CfdVelocityDivergenceField> moved2 = cfd::divergeCfdVelocity(
        after.topology, after.geometry, movedBoundary, velocityInput(movedValues));
    INFO((moved2.has_value() ? std::string{} : moved2.error().message));
    REQUIRE(moved2.has_value());

    for (std::size_t c = 0; c < cells; ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<CfdVelocityDivergenceField::Divergence> a = before->divergenceAt(cell);
        const std::optional<CfdVelocityDivergenceField::Divergence> b = moved2->divergenceAt(cell);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        const double bound = kFluxProductUnits * divergenceErrorScale(hand.topology, hand.geometry,
                                                                      cell, normOf(constant) + 10.0);
        INFO(std::format("cell {}: before {:.12e}, after {:.12e}, bound {:.3e}", c, a->si(), b->si(),
                         bound));
        CHECK(std::abs(a->si() - b->si()) <= bound);
    }
}

TEST_CASE("CfdDivergence_DividingTwiceGivesBitIdenticalFields", "[cfd][div]") {
    for (const Meshable& body : meshableModels()) {
        const Reference reference{body.kind};
        const PreparedCfdBoundary boundary = reference.wallEverywhere();
        const Mat tensor{0.25, 0.0, 0.0, 0.0, -0.75, 0.0, 0.0, 0.0, 1.25};
        const std::vector<double> values =
            linearVelocity(*reference.geometry, tensor, Vec{0.5, -0.25, 0.125});

        const Result<CfdVelocityDivergenceField> first = cfd::divergeCfdVelocity(
            *reference.topology, *reference.geometry, boundary, velocityInput(values));
        const Result<CfdVelocityDivergenceField> second = cfd::divergeCfdVelocity(
            *reference.topology, *reference.geometry, boundary, velocityInput(values));
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());

        // Bit-identical, by the field's own equality, which compares the stored
        // doubles and the summary.
        CHECK(*first == *second);
        for (std::size_t c = 0; c < first->cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            CHECK(first->at(cell)->si() == second->at(cell)->si());
        }
        CHECK(first->globalNetFlux().si() == second->globalNetFlux().si());
        CHECK(first->netBoundaryFlux().si() == second->netBoundaryFlux().si());
        CHECK(first->summary().relativeImbalance == second->summary().relativeImbalance);
    }
}

// ===========================================================================
// Cross-operator consistency
// ===========================================================================

TEST_CASE("CfdDivergence_TheVelocityDivergenceAgreesWithTheTraceOfTheGreenGaussGradient",
          "[cfd][div]") {
    // ADR-071 DECISION 6'S CORRECTNESS REQUIREMENT. The divergence and the
    // Green-Gauss gradient use the SAME face interpolation, so `div U` must equal
    // `trace(grad U)` -- not approximately, but to the rounding of two different
    // accumulation orders. If the weight were re-derived here with a different
    // form, this case would fail.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.outlets();
    const Mat tensor{0.5, 0.25, -0.125, 0.0, -1.5, 0.75, 0.25, 0.5, 2.0};
    const std::vector<double> values = linearVelocity(hand.geometry, tensor, Vec{1.0, -2.0, 3.0});

    const Result<CfdVelocityDivergenceField> divergence =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    INFO((divergence.has_value() ? std::string{} : divergence.error().message));
    REQUIRE(divergence.has_value());

    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        // The trace of the Green-Gauss gradient: the i-th component of the i-th
        // component's gradient.
        double trace = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            cfd::CfdVelocityGradientInput input;
            input.method = CfdGradientMethod::GreenGauss;
            input.component = cfd::kVectorComponents[i];
            input.currentVelocity = values;
            const Result<cfd::CfdVelocityGradientField> row =
                cfd::reconstructCfdVelocityGradient(hand.topology, hand.geometry, boundary, input);
            INFO((row.has_value() ? std::string{} : row.error().message));
            REQUIRE(row.has_value());
            const std::optional<cfd::CfdVelocityGradientField::Vector> g = row->at(cell);
            REQUIRE(g.has_value());
            trace += g->component(cfd::kVectorComponents[i]).si();
        }

        const std::optional<CfdVelocityDivergenceField::Divergence> got =
            divergence->divergenceAt(cell);
        REQUIRE(got.has_value());
        INFO(std::format("cell {}: div {:.12e}, trace(grad) {:.12e}", c, got->si(), trace));
        CHECK_THAT(got->si(), WithinRel(trace, 1.0e-12));
    }
}

// ===========================================================================
// Failure paths
// ===========================================================================

TEST_CASE("CfdDivergence_AStaleGeometryIsRefusedFirst", "[cfd][div]") {
    const Hand first = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand second = Hand::make(analyticPoints(), kAnalyticPair);

    // The second mesh's metrics against the first's topology, AND a wrong value
    // length, so the phase order decides which is reported.
    const std::vector<double> tooShort(1, 1.0);
    CHECK(cfd::cfdMassFluxDivergenceProblem(first.topology, second.geometry,
                                            massFluxInput(tooShort)) ==
          CfdDivergenceProblem::GeometryDescribesAnotherTopology);

    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(first.topology, second.geometry, massFluxInput(tooShort));
    REQUIRE_FALSE(field.has_value());
    CHECK_THAT(field.error().message, ContainsSubstring("different mesh generation"));
}

TEST_CASE("CfdDivergence_ANonFiniteValueIsRefusedAfterTheLength", "[cfd][div]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);

    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        std::vector<double> phi(hand.topology.faces(), 1.0);
        phi[2] = bad;
        CHECK(cfd::cfdMassFluxDivergenceProblem(hand.topology, hand.geometry, massFluxInput(phi)) ==
              CfdDivergenceProblem::ValueNotFinite);
        const Result<CfdMassFluxDivergenceField> field =
            cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
        REQUIRE_FALSE(field.has_value());
        CHECK_THAT(field.error().message, ContainsSubstring("not finite"));
        CHECK_THAT(field.error().message, ContainsSubstring("index 2"));
    }

    // AND THE ORDER: a wrong length with a non-finite value in it reports the
    // LENGTH, so a reader is not told about a value that was never going to be
    // read.
    std::vector<double> wrong(hand.topology.faces() + 1, std::numeric_limits<double>::quiet_NaN());
    CHECK(cfd::cfdMassFluxDivergenceProblem(hand.topology, hand.geometry, massFluxInput(wrong)) ==
          CfdDivergenceProblem::ValueLengthWrong);

    // AND THE VELOCITY PATH NAMES ITSELF, which nothing asserted. A review
    // found `View::name()`'s velocity branch unexercised: only the not-finite
    // diagnostics use it, and both were checked on the mass path, so making
    // `name()` return "mass flow rate" unconditionally survived. A diagnostic
    // that names the wrong quantity is the defect P19-GRAD-001 recorded as a
    // self-refuting message.
    const PreparedCfdBoundary boundary = hand.walls();
    std::vector<double> values = uniformVelocity(hand.topology.cells(), Vec{1.0, 2.0, 3.0});
    values[1] = std::numeric_limits<double>::quiet_NaN();
    CHECK(cfd::cfdVelocityDivergenceProblem(hand.topology, hand.geometry, boundary,
                                            velocityInput(values)) ==
          CfdDivergenceProblem::ValueNotFinite);
    const Result<CfdVelocityDivergenceField> refused =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not finite"));
    CHECK_THAT(refused.error().message, ContainsSubstring("velocity"));
    CHECK_THAT(refused.error().message, !ContainsSubstring("mass flow rate"));
}

TEST_CASE("CfdDivergence_ANonFiniteResultIsRefusedRatherThanReturned", "[cfd][div]") {
    // THE PHASE P19-GRAD-001'S REVIEW FOUND MISSING, built in from the start.
    // Every INPUT here is finite -- 1.8e308 passes the finiteness gate -- and the
    // surface sum overflows to infinities of each sign which cancel to a NaN.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const double huge = std::numeric_limits<double>::max();

    std::vector<double> phi(hand.topology.faces(), 0.0);
    // TWO FACES THIS CELL OWNS, so both contributions are POSITIVE and the sum
    // certainly overflows. Taking any two of `cellFaces` is not enough: a face
    // the cell merely neighbours is negated, the two cancel, the sum stays
    // finite, and there is then nothing for the output phase to catch -- which
    // is how a mutation deleting that phase survived this case.
    const CellIndex cell = CellIndex::fromValue(0);
    std::vector<FaceIndex> faces;
    for (const FaceIndex face : hand.topology.cellFaces(cell)) {
        if (hand.topology.owner(face).value() == cell.value()) {
            faces.push_back(face);
        }
    }
    INFO(std::format("cell 0 owns {} of its faces", faces.size()));
    REQUIRE(faces.size() >= 2);
    phi[faces[0].value()] = huge;
    phi[faces[1].value()] = huge;

    // The inputs are all finite, so the input gate passes...
    for (const double value : phi) {
        REQUIRE(std::isfinite(value));
    }
    CHECK(cfd::cfdMassFluxDivergenceProblem(hand.topology, hand.geometry, massFluxInput(phi)) ==
          std::nullopt);

    // ...and the OUTPUT phase is what catches it, if the sum is not finite.
    // The sum of two DBL_MAX values is infinite, so this input certainly
    // overflows and the refusal is REQUIRED rather than tolerated. A tolerant
    // if/else here is what let the deleted check survive.
    REQUIRE(huge + huge == std::numeric_limits<double>::infinity());
    const Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
    REQUIRE_FALSE(field.has_value());
    CHECK_THAT(field.error().message, ContainsSubstring("not finite"));

    // AND THE DIAGNOSTIC MUST NAME THE CELL, which is a claim about WHICH check
    // fired -- and it is the only thing a test can pin here.
    //
    // THE PER-CELL CHECK IS SUBSUMED BY THE TOTALS CHECK IN BEHAVIOUR: because
    // `globalNetFlux` is the sum of the per-cell values, it is non-finite
    // whenever any of them is, so deleting the per-cell loop STILL produces a
    // refusal. A mutation deleting it survived every assertion above, and
    // correctly so -- the outcome is unchanged. What changes is the message: the
    // totals check says "the totals are not finite" and names no cell, where the
    // per-cell check names the cell and the value it found. Saying WHERE is the
    // whole reason the per-cell loop exists, so that is what is asserted.
    CHECK_THAT(field.error().message, ContainsSubstring("at cell"));

    // The sharper version: a sum that definitely overflows. Four faces each at
    // max/2 cannot be represented... so use max on three faces of one cell.
    std::vector<double> worse(hand.topology.faces(), 0.0);
    for (std::size_t i = 0; i < faces.size(); ++i) {
        worse[faces[i].value()] =
            hand.topology.owner(faces[i]).value() == 0 ? huge : -huge;
    }
    const Result<CfdMassFluxDivergenceField> second =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(worse));
    if (second.has_value()) {
        for (std::size_t c = 0; c < second->cells(); ++c) {
            CHECK(std::isfinite(second->at(CellIndex::fromValue(c))->si()));
        }
        CHECK(std::isfinite(second->globalNetFlux().si()));
        CHECK(std::isfinite(second->netBoundaryFlux().si()));
    } else {
        CHECK_THAT(second.error().message, ContainsSubstring("not finite"));
    }
}

TEST_CASE("CfdDivergence_AVelocityDivergenceNeedsThreeValuesPerCell", "[cfd][div]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();

    // The CELL count is the plausible wrong answer for a reader who forgot the
    // layout is component-blocked.
    for (const std::size_t wrong : {cells, 3 * cells - 1, 3 * cells + 1}) {
        const std::vector<double> values(wrong, 1.0);
        CHECK(cfd::cfdVelocityDivergenceProblem(hand.topology, hand.geometry, boundary,
                                                velocityInput(values)) ==
              CfdDivergenceProblem::ValueLengthWrong);
        const Result<CfdVelocityDivergenceField> field =
            cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
        REQUIRE_FALSE(field.has_value());
        CHECK_THAT(field.error().message, ContainsSubstring("component-blocked velocity"));
    }
}

TEST_CASE("CfdDivergence_AStaleBoundaryIsRefusedOnTheVelocityPathOnly", "[cfd][div]") {
    const Hand twoCells = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand oneCell = Hand::make(oneCellPoints(), kOneCell);
    // The one-cell boundary has 4 facets, the two-cell topology has 6.
    const PreparedCfdBoundary wrongBoundary = oneCell.walls();
    REQUIRE(wrongBoundary.boundaryFaces() != twoCells.topology.boundaryFaces());

    const std::vector<double> values = uniformVelocity(twoCells.topology.cells(), Vec{1.0, 0.0, 0.0});
    CHECK(cfd::cfdVelocityDivergenceProblem(twoCells.topology, twoCells.geometry, wrongBoundary,
                                            velocityInput(values)) ==
          CfdDivergenceProblem::BoundaryDescribesAnotherTopology);

    // The MASS path cannot report it, because it has no boundary to be stale.
    const std::vector<double> phi(twoCells.topology.faces(), 1.0);
    CHECK(cfd::cfdMassFluxDivergenceProblem(twoCells.topology, twoCells.geometry,
                                            massFluxInput(phi)) == std::nullopt);
}

TEST_CASE("CfdDivergence_TheDeclaredPhaseOrderIsTheCheckOrder", "[cfd][div]") {
    // The order is asserted BETWEEN phases a constructible input can reach, by
    // presenting two faults at once and requiring the earlier phase.
    const Hand twoCells = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand oneCell = Hand::make(oneCellPoints(), kOneCell);

    // phase 1 before phase 2: stale geometry AND a stale boundary.
    {
        const PreparedCfdBoundary wrongBoundary = oneCell.walls();
        const std::vector<double> values =
            uniformVelocity(twoCells.topology.cells(), Vec{1.0, 0.0, 0.0});
        CHECK(cfd::cfdVelocityDivergenceProblem(twoCells.topology, oneCell.geometry, wrongBoundary,
                                                velocityInput(values)) ==
              CfdDivergenceProblem::GeometryDescribesAnotherTopology);
    }

    // phase 2 before phase 3: a stale boundary AND a wrong length.
    {
        const PreparedCfdBoundary wrongBoundary = oneCell.walls();
        const std::vector<double> tooShort(1, 1.0);
        CHECK(cfd::cfdVelocityDivergenceProblem(twoCells.topology, twoCells.geometry, wrongBoundary,
                                                velocityInput(tooShort)) ==
              CfdDivergenceProblem::BoundaryDescribesAnotherTopology);
    }

    // phase 3 before phase 4: a wrong length holding a NaN.
    {
        const PreparedCfdBoundary boundary = twoCells.walls();
        const std::vector<double> wrong(2, std::numeric_limits<double>::quiet_NaN());
        CHECK(cfd::cfdVelocityDivergenceProblem(twoCells.topology, twoCells.geometry, boundary,
                                                velocityInput(wrong)) ==
              CfdDivergenceProblem::ValueLengthWrong);
    }
}

TEST_CASE("CfdDivergence_ASuccessfulInputPredictsNoProblem", "[cfd][div]") {
    // RENAMED, because the old name claimed more than the case does. It read
    // `ThePredictorAgreesWithTheOperatorOnEveryFixture` and uses ONE fixture,
    // which a review pointed out; and "agrees with the operator" cannot be
    // tested at all here, because the predictor and the operator share one
    // decision path -- the limitation P19-DIFF-001 recorded as L8 and
    // P19-GRAD-001 as L9. What it pins is what its name now says: a successful
    // input predicts no problem, which a caller relies on.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();

    const std::vector<double> phi(hand.topology.faces(), 1.0);
    CHECK(cfd::cfdMassFluxDivergenceProblem(hand.topology, hand.geometry, massFluxInput(phi)) ==
          std::nullopt);
    CHECK(cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi)).has_value());

    const std::vector<double> values = uniformVelocity(hand.topology.cells(), Vec{1.0, 2.0, 3.0});
    CHECK(cfd::cfdVelocityDivergenceProblem(hand.topology, hand.geometry, boundary,
                                            velocityInput(values)) == std::nullopt);
    CHECK(cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values))
              .has_value());

    // AND THE PREDICTOR CANNOT SEE THE OUTPUT PHASE, which the header states: it
    // judges an output it never computes. An input that passes every input phase
    // predicts nothing even if the sum would overflow.
    std::vector<double> huge(hand.topology.faces(), 0.0);
    huge[0] = std::numeric_limits<double>::max();
    CHECK(cfd::cfdMassFluxDivergenceProblem(hand.topology, hand.geometry, massFluxInput(huge)) ==
          std::nullopt);
}

TEST_CASE("CfdDivergence_TheWorstSkewnessIsReportedOnInterpolatedFacesOnly", "[cfd][div]") {
    // Skewness is raised on INTERNAL faces only, because that is the only place a
    // value is interpolated. A one-cell mesh has no internal face, so it must
    // report exactly zero -- and a reference body must report more.
    const Hand oneCell = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(oneCell.topology.internalFaces() == 0);
    const PreparedCfdBoundary boundary = oneCell.walls();
    const std::vector<double> values = uniformVelocity(oneCell.topology.cells(), Vec{1.0, 2.0, 3.0});
    const Result<CfdVelocityDivergenceField> field =
        cfd::divergeCfdVelocity(oneCell.topology, oneCell.geometry, boundary, velocityInput(values));
    REQUIRE(field.has_value());
    CHECK(field->summary().worstSkewness == 0.0);

    // On the reference bodies there ARE internal faces, and the reported figure
    // must equal the maximum computed independently over each body's own faces.
    //
    // THE COUNT IS NOT ASSERTED PER BODY, and the first version of this case did
    // assert it: `internalFaces() > 100` fails on the Block, which has exactly
    // SIX. That is the identical trap P19-GRAD-001 hit -- its first attempt
    // asserted `internal > 10` on the same body for the same reason -- so the
    // claim is made where it is true: over the whole set, and with the per-body
    // requirement only that the reduction sees more than one face.
    std::size_t totalInternal = 0;
    std::size_t widest = 0;
    for (const Meshable& body : meshableModels()) {
        const Reference reference{body.kind};
        const PreparedCfdBoundary walls = reference.wallEverywhere();
        const std::vector<double> many =
            uniformVelocity(reference.topology->cells(), Vec{1.0, 2.0, 3.0});
        const Result<CfdVelocityDivergenceField> big = cfd::divergeCfdVelocity(
            *reference.topology, *reference.geometry, walls, velocityInput(many));
        INFO((big.has_value() ? std::string{} : big.error().message));
        REQUIRE(big.has_value());

        double expected = 0.0;
        std::size_t measured = 0;
        for (std::size_t f = 0; f < reference.topology->faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            if (!reference.topology->isInternal(face)) {
                continue;
            }
            ++measured;
            if (const std::optional<double> skew = reference.geometry->skewness(face);
                skew.has_value()) {
                expected = std::max(expected, std::abs(*skew));
            }
        }
        INFO(std::format("{}: {} internal faces, worst skewness {:.6e}", body.name, measured,
                         expected));
        // The reported figure IS the independent maximum, bit for bit.
        CHECK(big->summary().worstSkewness == expected);
        CHECK(expected > 0.0);
        // The reduction saw more than one face, so a max that was really an
        // assignment would be visible on this body.
        CHECK(measured > 1);
        CHECK(measured == reference.topology->internalFaces());
        totalInternal += measured;
        widest = std::max(widest, measured);
    }
    // And somewhere in the set the reduction runs over a great many faces, which
    // is the claim the per-body count could not carry.
    INFO(std::format("{} internal faces over the set, widest body {}", totalInternal, widest));
    CHECK(totalInternal > 1000);
    CHECK(widest > 100);
}

TEST_CASE("CfdDivergence_TheSummaryCountsMatchTheTopology", "[cfd][div]") {
    for (const Meshable& body : meshableModels()) {
        const Reference reference{body.kind};
        const std::vector<double> phi(reference.topology->faces(), 1.0);
        const Result<CfdMassFluxDivergenceField> field =
            cfd::divergeCfdMassFlux(*reference.topology, *reference.geometry, massFluxInput(phi));
        REQUIRE(field.has_value());
        const CfdDivergenceSummary& summary = field->summary();
        CHECK(summary.cells == reference.topology->cells());
        CHECK(summary.internalFaces == reference.topology->internalFaces());
        CHECK(summary.boundaryFaces == reference.topology->boundaryFaces());
        CHECK(summary.internalFaces + summary.boundaryFaces == reference.topology->faces());
        CHECK(field->cells() == reference.topology->cells());
        // AND THE MASS PATH REPORTS NO SKEWNESS ON A MESH THAT HAS THOUSANDS OF
        // INTERNAL FACES, which is where that claim belongs. It used to be
        // asserted on a one-cell fixture with no internal face at all, where
        // `skewness` is empty by contract and the figure is 0.0 for every
        // implementation; hoisting the skewness reduction above the mass path's
        // early return survived it. Here there is skewness to find, and the mass
        // path must still report none, because it interpolates nothing.
        CHECK(reference.topology->internalFaces() > 1);
        CHECK(summary.worstSkewness == 0.0);
    }
}

// ===========================================================================
// What two independent reviews found unpinned
// ===========================================================================

TEST_CASE("CfdDivergence_AnInternalFacesTwoVelocityContributionsSumToExactlyZero", "[cfd][div]") {
    // THE SAME STRUCTURAL CLAIM AS THE MASS-PATH CASE, ON THE PATH WHERE IT IS
    // NOT FREE. A review established that the mass-path case cannot fail: there
    // `faceFlux` is one array slot read by index, so a GATHER would read the
    // same `phi[f]` from both sides and negate it, and the assertion holds for
    // any implementation. ADR-071 Decision 2 is a claim about the VELOCITY path,
    // where a gather would re-derive the interpolated face value per cell and
    // the two readings could differ in the last bits.
    //
    // The method: a NON-CONSTANT velocity, so the interpolation actually does
    // something, and every boundary facet a slip wall -- whose flux is exactly
    // 0.0 by Decision 7 -- so each cell's net outflow is that one internal
    // face's contribution and nothing else.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    REQUIRE(hand.topology.cells() == 2);
    REQUIRE(hand.topology.internalFaces() == 1);
    const PreparedCfdBoundary boundary = hand.slips();

    std::vector<double> values(3 * hand.topology.cells(), 0.0);
    // Cell 0 and cell 1 get DIFFERENT velocities, or `U_N + w (U_P - U_N)` would
    // be `U` for every weight and the interpolation would be invisible.
    values[0] = kDiscriminatingConstant;
    values[1] = -3.0 * kDiscriminatingConstant;
    values[2] = 2.0;
    values[3] = -5.0;
    values[4] = 7.5;
    values[5] = kDiscriminatingConstant / 3.0;

    const Result<CfdVelocityDivergenceField> field =
        cfd::divergeCfdVelocity(hand.topology, hand.geometry, boundary, velocityInput(values));
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    const std::optional<VolumetricFlowRate> first = field->at(CellIndex::fromValue(0));
    const std::optional<VolumetricFlowRate> second = field->at(CellIndex::fromValue(1));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    // EXACTLY, not WithinAbs. This is the claim.
    CHECK(first->si() + second->si() == 0.0);
    // And neither is itself zero, so the sum is not trivially zero -- which on
    // this path needs the two velocities to differ.
    CHECK(first->si() != 0.0);
    CHECK(second->si() != 0.0);
    // The whole mesh is slip-walled, so the boundary contributes exactly nothing
    // and the global total is exactly zero as well.
    CHECK(field->netBoundaryFlux().si() == 0.0);
    CHECK(field->globalNetFlux().si() == 0.0);
}

TEST_CASE("CfdDivergence_TheInterpolationWeightMatchesItsClosedFormAndVariesOnARealMesh",
          "[cfd][div]") {
    // THE WEIGHT WAS UNPINNED, and a review showed how completely: every
    // internal face the velocity path ever interpolated across under an
    // assertion has `w = 2/3` EXACTLY, on both hand fixtures, so
    // `interpolationWeight` replaced by `return 2.0 / 3.0;` survived the entire
    // suite. The oracle that might have caught it was a transcription of the
    // formula, and `CfdGradient` holds a third copy of the same expression.
    SECTION("the closed form the analytic pair was built for, and never asserted") {
        const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
        REQUIRE(hand.topology.internalFaces() == 1);
        const FaceIndex internal = FaceIndex::fromValue(0);
        REQUIRE(hand.topology.isInternal(internal));

        // The fixture's own docstring promises `|d| = 5/4`, `d . n = 3/4` and
        // `cos(theta) = 3/5`, all exact. A review found that NOT ONE of the three
        // was asserted anywhere in the file, so the fixture's distinguishing
        // property was documentation rather than evidence.
        const std::optional<Translation3D> span = hand.geometry.ownerToNeighbour(internal);
        REQUIRE(span.has_value());
        const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(internal);
        REQUIRE(normal.has_value());
        const Vec d = siOf(*span);
        const Vec n{normal->x(), normal->y(), normal->z()};
        const double lengthOfD = normOf(d);
        const double projection = std::abs(dotOf(d, n));
        INFO(std::format("|d| = {:.17g}, d.n = {:.17g}, cos = {:.17g}", lengthOfD, projection,
                         projection / lengthOfD));
        CHECK_THAT(lengthOfD, WithinRel(5.0 / 4.0, 1.0e-15));
        CHECK_THAT(projection, WithinRel(3.0 / 4.0, 1.0e-15));
        CHECK_THAT(projection / lengthOfD, WithinRel(3.0 / 5.0, 1.0e-15));
        CHECK_THAT(hand.geometry.orthogonalDistance(internal).si(), WithinRel(3.0 / 4.0, 1.0e-15));

        // And the weight itself, from the INDEPENDENT oracle, against the closed
        // form rather than against production's expression.
        const double w = weightOracle(hand.topology, hand.geometry, internal);
        INFO(std::format("w = {:.17g}", w));
        CHECK_THAT(w, WithinRel(2.0 / 3.0, 1.0e-15));
    }

    SECTION("and it VARIES on a real mesh, where the operator is checked against the oracle") {
        // The hand fixtures cannot see a constant weight, because theirs IS
        // 2/3. A reference body has thousands of internal faces whose weights
        // genuinely differ, and the velocity path's per-cell answer there was
        // compared against nothing at all.
        const Reference reference{bettercad::reference::MeshReferenceModelKind::Cylinder};
        const PreparedCfdBoundary boundary = reference.wallEverywhere();

        double lowest = 1.0;
        double highest = 0.0;
        std::size_t away = 0;
        const std::size_t internalFaces = reference.topology->internalFaces();
        // MEASURED, NOT ASSUMED: the Cylinder has 946 internal faces. The first
        // draft of this case asserted `> 1000` by analogy with the closure
        // case's `totalInternal > 1000`, which is a sum over ALL EIGHT bodies
        // and not one of them -- the same trap that caught this milestone with
        // `internalFaces() > 100` on the Block, which has six, and
        // P19-GRAD-001 with `> 10` on the same body. The claim needs many faces
        // whose weights differ, and `away` below is what actually establishes
        // that.
        REQUIRE(internalFaces > 100);
        for (std::size_t f = 0; f < internalFaces; ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            REQUIRE(reference.topology->isInternal(face));
            const double w = weightOracle(*reference.topology, *reference.geometry, face);
            lowest = std::min(lowest, w);
            highest = std::max(highest, w);
            if (std::abs(w - 2.0 / 3.0) > 1.0e-6) {
                ++away;
            }
        }
        INFO(std::format("{} internal faces, w in [{:.6f}, {:.6f}], {} away from 2/3",
                         internalFaces, lowest, highest, away));
        // A CONVEX weight, strictly inside (0, 1), which is ADR-069's claim.
        CHECK(lowest > 0.0);
        CHECK(highest < 1.0);
        // AND NOT A CONSTANT: most of them are nowhere near 2/3, so a hard-coded
        // weight cannot survive the comparison below.
        CHECK(away > internalFaces / 2);

        // The operator's own interpolated answer, against the independent
        // oracle, on a linear field -- so the weight is load-bearing for the
        // expected value and a constant weight gives a different number.
        const Mat tensor{0.7, -0.2, 0.4, 0.1, -0.5, 0.3, -0.6, 0.8, 0.2};
        const Vec offset{1.5, -2.5, 0.25};
        const std::vector<double> values = linearVelocity(*reference.geometry, tensor, offset);
        const Result<CfdVelocityDivergenceField> field = cfd::divergeCfdVelocity(
            *reference.topology, *reference.geometry, boundary, velocityInput(values));
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        double worst = 0.0;
        double bound = 0.0;
        const double scaleOfU = normOf(offset) + normOf(Vec{1.0, 1.0, 1.0});
        for (std::size_t c = 0; c < reference.topology->cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            const std::optional<VolumetricFlowRate> stored = field->at(cell);
            REQUIRE(stored.has_value());
            const double expected = velocityFluxSumOracle(*reference.topology, *reference.geometry,
                                                          boundary, values, cell);
            worst = std::max(worst, std::abs(stored->si() - expected));
            bound = std::max(bound, kFluxProductUnits *
                                        divergenceErrorScale(*reference.topology,
                                                             *reference.geometry, cell, scaleOfU) *
                                        reference.geometry->cellVolume(cell).si());
        }
        INFO(std::format("worst |operator - oracle| = {:.6e}, bound {:.6e}", worst, bound));
        CHECK(worst <= bound);
    }
}

TEST_CASE("CfdDivergence_TheLargestMagnitudeIsTheMaximumAbsoluteNetOutflow", "[cfd][div]") {
    // A REDUCTION OVER AN ALL-ZERO SET CANNOT BE WRONG. A review found
    // `largestMagnitude` had exactly one assertion in the suite, inside the case
    // where every flux is zero, so `return Q::fromSi(0.0)`, `std::min` for
    // `std::max`, returning the first cell, and dropping `std::abs` all
    // survived. The header promises "the largest |net outflow| over the mesh".
    SECTION("the maximum, on a fixture whose two cells differ") {
        // The owner-sign fixture: two faces out of cell 0 and one out of cell 1,
        // giving net outflows of exactly 4.0 and 2.0.
        const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
        const std::vector<double> phi(hand.topology.faces(), 1.0);
        const Result<CfdMassFluxDivergenceField> field =
            cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
        REQUIRE(field.has_value());

        double expected = 0.0;
        std::vector<double> each;
        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            const std::optional<MassFlowRate> value = field->at(CellIndex::fromValue(c));
            REQUIRE(value.has_value());
            each.push_back(value->si());
            expected = std::max(expected, std::abs(value->si()));
        }
        REQUIRE(each.size() == 2);
        INFO(std::format("cells {:.17g} and {:.17g}", each[0], each[1]));
        // The two must DIFFER, or a maximum and an assignment agree.
        REQUIRE(each[0] != each[1]);
        CHECK(field->largestMagnitude().si() == expected);
        // And it is the larger of the two by magnitude, not the first, not the
        // last and not their sum.
        CHECK(field->largestMagnitude().si() == std::max(std::abs(each[0]), std::abs(each[1])));
        CHECK(field->largestMagnitude().si() != std::min(std::abs(each[0]), std::abs(each[1])));
        CHECK(field->largestMagnitude().si() != std::abs(each[0] + each[1]));
    }

    SECTION("ABSOLUTE, so a field whose every cell is negative reports a positive figure") {
        // Dropping `std::abs` would return 0.0 here, since every value is below
        // zero. This is the half of the claim the word "magnitude" carries.
        const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
        const std::vector<double> phi(hand.topology.faces(), -1.0);
        const Result<CfdMassFluxDivergenceField> field =
            cfd::divergeCfdMassFlux(hand.topology, hand.geometry, massFluxInput(phi));
        REQUIRE(field.has_value());

        double mostNegative = 0.0;
        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            const std::optional<MassFlowRate> value = field->at(CellIndex::fromValue(c));
            REQUIRE(value.has_value());
            CHECK(value->si() < 0.0);
            mostNegative = std::min(mostNegative, value->si());
        }
        INFO(std::format("most negative cell {:.17g}", mostNegative));
        CHECK(field->largestMagnitude().si() == -mostNegative);
        CHECK(field->largestMagnitude().si() > 0.0);
    }
}

