// P19-GEOM-001: the finite-volume geometric metrics (ADR-065).
//
// THREE KINDS OF EVIDENCE, because the three claims are different in kind.
//
//   1. THE FORMULAS ARE RIGHT. Checked against CLOSED FORMS on shapes whose
//      metrics are known by hand -- the unit tetrahedron, a 3-4-5 right
//      triangle, an equilateral triangle -- not against the code. These run on
//      the point-level functions, where a degenerate input can be handed in
//      directly.
//   2. THE ASSEMBLY IS RIGHT. Checked on every P16 reference mesh, with every
//      structural claim run on EVERY one of them (`checkStructure`). That is
//      P19-TOPO-001's lesson: a review there found each structural property
//      being checked on the one model chosen for it, which left the transformed
//      body untested.
//   3. THE NUMBERS AGREE WITH AN ORACLE -- and the oracles are NOT all equally
//      independent, which a test-gap review found this comment glossing over.
//      Provenance is not independence: a number computed by another milestone
//      from the same formula agrees however wrong the formula is.
//
//      analyticVolumeMm3        INDEPENDENT. A closed form over the model's own
//                               dimensions, read from its parameters.
//      OCCT's cadVolume()       INDEPENDENT. The kernel integrating the exact
//                               solid, nothing discretised.
//      2(ab + bc + ca)          INDEPENDENT, and the only oracle for the face
//                               AREAS: a volume can be right while every area
//                               is wrong.
//      tetrahedralVolume()      NOT independent -- it calls the same
//                               `meshing::signedVolume` over the same elements
//                               in the same order. A PLUMBING check: every cell
//                               visited once, none double-counted.
//      boundaryVolume()         Adds nothing. `generateVolumeMesh` already gates
//                               |tet - enclosed|/enclosed <= 1e-9, so a mesh
//                               that reaches a test here satisfies it already.
//
//      For the two QUALITY metrics the position is weaker still, and is stated
//      rather than glossed: P19-MESH-001's recorded numbers and the measurement
//      in this file are two TRANSCRIPTIONS of one definition, so their agreement
//      checks the plumbing -- owner selection, winding, ordering, units, the
//      clamp -- and not the definition. What checks the definition is
//      CfdGeometry_AnAnalyticFacePairFixesTheAngleAndTheSkewnessInClosedForm,
//      where the angle is arccos(3/5) and the skewness sqrt(10)/15 by hand.
//
// WHAT IS CHECKED INDEPENDENTLY RATHER THAN AGAINST THE BUILDER. Every centroid
// is recomputed here from the mesh's own nodes; every area is recomputed from the
// area vector's components rather than read back from `faceArea`, which IS
// `magnitude(areaVector)` and would be a tautology; each cell's volume is checked
// a second time by the divergence theorem over its own faces, which uses the face
// metrics and not the determinant; and a boundary normal is checked against the
// owner's centroid, which is independent of the winding the builder used.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using bettercad::reference::MeshReferenceModelKind;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdGeometry;
using cfd::CfdTopology;
using cfd::FaceIndex;
using meshing::ElementId;
using meshing::NodeId;

namespace {

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// ADR-063's face table, written out again HERE rather than included from the
/// header, so a fixture that agreed with a corrupted table would not agree with
/// this one.
constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

// ---------------------------------------------------------------------------
// Tolerances, each with its reason
// ---------------------------------------------------------------------------

/// Well-conditioned double-precision algebra: a closed form evaluated on small
/// integers and halves, where only the last bit is in doubt. The repository's
/// figure, used for the analytic primitives.
constexpr double kExact = 1e-12;

/// A relative bound for a quantity accumulated over a mesh. The repository's
/// figure for geometric accumulation is 1e-9; every measured value under it here
/// is reported by the tests that use it, so the margin is visible rather than
/// assumed.
constexpr double kAccumulated = 1e-9;

/// The dimensionless closure residual `|sum Sf| / sum |Sf|`, and the relative
/// error of the divergence-theorem volume.
///
/// WHY NOT 1e-15, WITH THE MEASUREMENT RATHER THAN AN ESTIMATE. Both are
/// cancellations of products of COORDINATE DIFFERENCES: a cell of size d whose
/// vertices sit at distance L from the origin forms those differences with an
/// absolute error of order eps*L, so the cancelled sum's relative error is of
/// order eps*L/d, and these meshes reach cell volume ratios of 1.5e-4.
///
/// Measured over all eight reference meshes and every fixture here, the worst
/// values are 2.03e-16 for the closure residual and 1.21e-14 for the divergence
/// identity -- both reported by the tests that assert them. This bound is about
/// eighty times the worst. (The reason below cited `acos` as well; no assertion
/// that uses this bound goes through it. The closure residual goes through
/// `hypot` and the divergence identity through no libm at all.)
///
/// WHY THAT MARGIN, CORRECTED. It was first justified by GCC's FMA contraction,
/// and an adversarial review disproved that on this toolchain: `-ffp-contract=fast`
/// is indeed the default, but the presets set no `-march`, so on x86-64 baseline
/// no FMA is emitted at all (checked: `-O3` gives mulsd/addsd, `-O3 -march=haswell`
/// gives vfmadd231sd). The real reasons the margin is not tightened to the
/// measurement are that `magnitude` and the quality metrics go through libm
/// (`hypot`, `acos`), which is not required to be identically rounded between
/// builds; that the eight reference meshes span cell volume ratios of 1.5e-4 and a
/// ninth model would widen the spread; and that this bound is asserted per cell
/// and per face over roughly ten thousand of them.
constexpr double kClosure = 1e-12;

/// The agreement required with the two quality figures P19-MESH-001 recorded.
/// Those were printed to 2 and 4 decimal places, so this is the precision of the
/// record and not a looseness in the computation: the two code paths accumulate
/// the centroids differently (`c += p/4` against `(sum p)/4`), which is
/// deliberate -- identical arithmetic would make the comparison a restatement of
/// the formula rather than a check of it.
constexpr double kRecordedAngleDeg = 0.005;
constexpr double kRecordedSkewness = 0.00005;

/// The absolute bound, in radians, on a non-orthogonality angle that should be
/// ZERO, under a transform that should not change it.
///
/// NOT A LOOSENING, A CONDITION NUMBER. `acos` is ill-conditioned at 1:
/// d(theta)/d(cos) = -1 / sin(theta), which diverges as theta -> 0, so a cosine
/// perturbed by one eps becomes an angle of sqrt(2 eps) ~ 2.11e-8 rad. Measured
/// on the rigidly moved two-cell mesh, where the true angle is exactly zero, the
/// difference is 2.107e-8 rad -- sqrt(2 eps) to three significant figures. The
/// COSINE is well-conditioned and is checked to 1e-14 in the same case, which is
/// what shows the loss belongs to `acos` and not to the geometry.
constexpr double kAngleNearZero = 1.0e-7;

/// The absolute bound on a non-orthogonality COSINE under a transform that should
/// not change it.
///
/// SET FROM A MEASUREMENT, after 1e-14 -- calibrated on the well-conditioned
/// two-cell fixture, where the cosine moves by 8.9e-16 -- failed on RM-MESH-02.
/// Measured worst over all eight reference meshes: 2.69e-14, and this bound is
/// about forty times that.
///
/// THE MECHANISM, CORRECTED. A first version of this reason said the cosine's
/// error is of order `eps / cos`, taking a RELATIVE error of `d . n` and using it
/// as an ABSOLUTE bound on the quotient; divide through and the `1/cos` cancels.
/// The absolute error is of order `eps * L / |d|`, where L is the distance of the
/// nodes from the model origin, because `d` is a difference of two centroids each
/// carrying `eps * L`. Measured here rather than argued: driving `cos` from
/// 0.9918 to 0.0920 on a fixed fixture leaves the error at or below 5.6e-17,
/// while translating that same fixture from the origin to 10 km raises it from 0
/// to 8.2e-13 as `L / |d|` goes from 3.6 to 6.3e4.
///
/// SO THIS BOUND IS CALIBRATED TO MODELS NEAR THE ORIGIN. The suite reaches
/// `L / |d|` of a few hundred -- RM-MESH-06's placed copy is offset by 43 mm --
/// and a model placed kilometres away would exceed it. A reader who sees this
/// fail should look at the origin distance, not at the cosine.
///
/// It is still the right quantity to assert on rather than the angle, whose loss
/// at the same place is `sqrt(2 eps)` -- six orders worse -- and that contrast is
/// the whole content of ADR-065's conditioning note.
constexpr double kCosineInvariance = 1.0e-12;

/// The absolute bound on a dimensionless quality metric under the same transform.
/// Skewness is a ratio of two lengths of the same order, but its crossing point
/// comes through the same cancellation as the cosine, and through a division by
/// `d . Sf` as well. Measured worst over the eight reference meshes: 9.86e-14, on
/// RM-MESH-03.
///
/// WHY THIS MARGIN IS WIDER THAN THE COSINE'S -- a thousandfold against fortyfold,
/// an asymmetry a review asked about. The crossing point `C_P + lambda d` carries
/// the cosine's error AND `lambda`'s, and `lambda` is itself a ratio of two
/// cancelled volumes, so the skewness inherits a product of two ill-conditioned
/// quantities where the cosine inherits one. The extra margin is for that second
/// factor, which no measurement here bounds independently.
constexpr double kQualityInvariance = 1.0e-10;

/// The relative bound between the production metrics and the independent
/// measurement written in this file. The two differ only in how they accumulate a
/// centroid (`(sum p)/4` against `sum (p/4)`) and in normalising the face normal
/// before the dot product, so a few eps of accumulated difference is expected;
/// 1e-9 is far above it and is the bound a DEFINITION error would have to hide
/// under, which is why the closed-form anchors matter more than this number.
constexpr double kAgreement = 1.0e-9;

/// The relative bound for the kernel's own volume against the catalogue's closed
/// form: the repository's figure for well-conditioned double-precision algebra,
/// and the same constant the P16 reference suite uses for this comparison.
constexpr double kCadVolume = 1.0e-12;

// ---------------------------------------------------------------------------
// Independent vector arithmetic, written here
// ---------------------------------------------------------------------------

using Vec = std::array<double, 3>;

Vec si(const Point3D& p) { return {p.x.si(), p.y.si(), p.z.si()}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dotp(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec crossp(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double norm(const Vec& a) { return std::sqrt(dotp(a, a)); }
Vec siOf(const AreaVector3D& s) { return {s.x.si(), s.y.si(), s.z.si()}; }
Vec siOf(const Translation3D& t) { return {t.x.si(), t.y.si(), t.z.si()}; }

/// A length parameter's value in mm, read from the document. The same helper the
/// P16 reference suite uses, so a dimension is read from the model and never from
/// a volume the code under test produced.
[[nodiscard]] double mm(const Document& document, std::string_view name) {
    const std::optional<ObjectId> object = document.findByName(name);
    REQUIRE(object.has_value());
    const std::optional<ParameterId> id = document.asParameter(*object);
    REQUIRE(id.has_value());
    const Parameter* parameter = document.parameters().find(*id);
    REQUIRE(parameter != nullptr);
    return Length::fromSi(parameter->siValue()).in(units::mm);
}

// ---------------------------------------------------------------------------
// Hand-built meshes
// ---------------------------------------------------------------------------

/// Builds a mesh from @p points and @p tets and covers every face exactly one tet
/// uses with a boundary triangle wound outward of that tet.
meshing::Mesh buildMesh(const std::vector<Point3D>& points, const std::vector<std::array<std::size_t, 4>>& tets) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> nodes;
    for (const Point3D& p : points) {
        nodes.push_back(require(builder.addNode(p)));
    }
    std::map<std::array<std::size_t, 3>, std::pair<std::size_t, std::array<std::size_t, 3>>> uses;
    for (const auto& tet : tets) {
        REQUIRE(builder.addTetrahedron({nodes[tet[0]], nodes[tet[1]], nodes[tet[2]], nodes[tet[3]]}, kRegion));
        for (const auto& face : kFaces) {
            const std::array<std::size_t, 3> wound{tet[face[0]], tet[face[1]], tet[face[2]]};
            std::array<std::size_t, 3> key = wound;
            std::ranges::sort(key);
            auto& [count, winding] = uses[key];
            if (count++ == 0) {
                winding = wound;
            }
        }
    }
    for (const auto& [key, use] : uses) {
        const auto& [count, w] = use;
        if (count >= 2) {
            continue;
        }
        REQUIRE(builder.addTriangle({nodes[w[0]], nodes[w[1]], nodes[w[2]]}, kRegion));
    }
    return builder.build();
}

/// Two cells sharing one face: A = (0,1,2,3), B = (1,2,3,4), both positive.
/// 7 faces: 1 interior, 6 boundary.
///
/// THE SIXTH POINT IS AN ORPHAN and is kept deliberately. No element references
/// node 5 at (20, 20, 20) mm; it is carried verbatim from
/// CfdTopologyTests.cpp's fixture so that the two milestones measure the same
/// geometry, and ADR-065 quotes this fixture's centroids and its
/// 1.000000000000000222 cosine by value. Changing the point set would change
/// those numbers. The topology walks elements, so an unreferenced node reaches
/// nothing here except `mesh.bounds()`, which no case reads OF THIS FIXTURE (two
/// do read it, of the RM-MESH-06 pair). An adversarial review noticed the orphan;
/// it is noted rather than removed.
std::vector<Point3D> twoCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm},    Point3D{10_mm, 0_mm, 0_mm},   Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm},   Point3D{10_mm, 10_mm, 10_mm}, Point3D{20_mm, 20_mm, 20_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};

/// The unit tetrahedron, in metres: every metric of it is known in closed form.
std::vector<Point3D> unitTetPoints() {
    return {Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(1.0), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(0.0), Length::fromSi(1.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(1.0)}};
}

/// One patch holding every boundary triangle of @p mesh.
std::vector<meshing::MappedFace> onePatch(const meshing::Mesh& mesh) {
    meshing::MappedFace face;
    face.index = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        face.facets.push_back(triangle.id);
    }
    return {face};
}

/// @p mesh with every node moved by @p motion, and the SAME connectivity in the
/// SAME order, so the topology of the two is identical index for index and the
/// metrics can be compared one to one. A remeshed rotated body could not be
/// compared this way, which is why the transform is applied node by node here.
meshing::Mesh transformed(const meshing::Mesh& mesh, const RigidTransform3D& motion) {
    meshing::MeshBuilder builder;
    for (const meshing::Node& node : mesh.nodes()) {
        REQUIRE(builder.addNode(node.id, motion.apply(node.position)));
    }
    // ONE ASCENDING PASS OVER BOTH KINDS, because MeshBuilder requires strictly
    // increasing element handles and P16's canonical order is every Triangle3 then
    // every Tetrahedron4 -- which is NOT ascending in the handle when a mesh holds
    // both. The two spans are merged by handle here.
    std::size_t t = 0;
    std::size_t q = 0;
    const std::span<const meshing::Triangle> triangles = mesh.triangles();
    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
    while (t < triangles.size() || q < tets.size()) {
        const bool takeTriangle = q == tets.size() || (t < triangles.size() && triangles[t].id < tets[q].id);
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

// ---------------------------------------------------------------------------
// The reference models, meshed
// ---------------------------------------------------------------------------

void assignWater(Document& document) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> id = features::createMaterial(document, "CfdWater", water);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// A P16 reference model, meshed, with water assigned and the CFD input prepared.
struct MeshedFluid {
    meshref::MeshedReference model;

    explicit MeshedFluid(MeshReferenceModelKind kind) : MeshedFluid(document(kind)) {}

    explicit MeshedFluid(Document built) : model(std::move(built)) {
        assignWater(model.document());
        (void)model.regenerate();
        (void)model.require();
    }

    static Document document(MeshReferenceModelKind kind) {
        Result<Document> built = bettercad::reference::buildMeshReferenceModel(kind);
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    [[nodiscard]] Result<cfd::CfdModel> prepare() {
        return cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
    }
};

/// A model prepared all the way to its metrics, which is what most cases below
/// want: the mesh, its topology and its geometry, all current together.
struct PreparedFluid {
    MeshedFluid fluid;
    Result<cfd::CfdModel> model;
    Result<cfd::CfdMeshContract> contract;
    Result<CfdTopology> topology;
    Result<CfdGeometry> geometry;

    explicit PreparedFluid(MeshReferenceModelKind kind)
        : fluid(kind), model(fluid.prepare()), contract(model.has_value() ? cfd::requireCfdMesh(*model)
                                                                        : std::unexpected(model.error())),
          topology(contract.has_value() ? cfd::requireCfdTopology(*model, *contract)
                                        : std::unexpected(contract.error())),
          geometry(topology.has_value() ? cfd::requireCfdGeometry(*model, *topology)
                                        : std::unexpected(topology.error())) {
        INFO((model.has_value() ? std::string{} : model.error().message));
        REQUIRE(model.has_value());
        REQUIRE(contract.has_value());
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        INFO((geometry.has_value() ? std::string{} : geometry.error().message));
        REQUIRE(geometry.has_value());
    }

    [[nodiscard]] const meshing::Mesh& mesh() const { return model->mesh().mesh(); }
    [[nodiscard]] const meshing::VolumeMesh& volumeMesh() const { return model->mesh(); }
};

// ---------------------------------------------------------------------------
// The independent quality measurement
// ---------------------------------------------------------------------------

/// The two ADR-063 quality measures, from the definitions alone, over the faces
/// the topology calls internal.
///
/// NOT A COPY OF P19-MESH-001's `measure()`: that one derives its own owner map
/// from the mesh, which is the half this milestone does not need to re-derive.
/// This one takes the topology's owner, neighbour and winding -- the things
/// ADR-064 already qualified -- and computes only the two ANGLES AND RATIOS from
/// the node coordinates, which is what P19-GEOM-001 added. The comparison with
/// P19-MESH-001's RECORDED numbers covers the other half.
struct Quality {
    double maxNonOrthogonalityDeg = 0.0;
    double meanNonOrthogonalityDeg = 0.0;
    double maxSkewness = 0.0;
    std::size_t faces = 0;
};

Quality independentQuality(const CfdTopology& topology, const meshing::Mesh& mesh) {
    const auto at = [&](NodeId id) {
        const meshing::Node* node = mesh.findNode(id);
        REQUIRE(node != nullptr);
        return si(node->position);
    };
    const auto centroidOf = [&](CellIndex cell) {
        const meshing::Tetrahedron* tet = mesh.findTetrahedron(topology.cellOf(cell));
        REQUIRE(tet != nullptr);
        Vec c{};
        for (const NodeId id : tet->nodes) {
            const Vec p = at(id);
            for (std::size_t i = 0; i < 3; ++i) {
                c[i] += p[i] / 4.0;
            }
        }
        return c;
    };
    Quality q;
    double sum = 0.0;
    for (std::size_t f = 0; f < topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::array<NodeId, 3> nodes = topology.faceNodes(face);
        const Vec a = at(nodes[0]);
        const Vec b = at(nodes[1]);
        const Vec c = at(nodes[2]);
        const Vec s = crossp(sub(b, a), sub(c, a)); // 2 Sf; only its direction matters
        const Vec cf{(a[0] + b[0] + c[0]) / 3.0, (a[1] + b[1] + c[1]) / 3.0, (a[2] + b[2] + c[2]) / 3.0};
        const Vec cp = centroidOf(topology.owner(face));
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        REQUIRE(neighbour.has_value());
        const Vec d = sub(centroidOf(*neighbour), cp);
        const double cosine = dotp(s, d) / (norm(s) * norm(d));
        const double angle = std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        const double lambda = dotp(sub(cf, cp), s) / dotp(d, s);
        const Vec x{cp[0] + lambda * d[0], cp[1] + lambda * d[1], cp[2] + lambda * d[2]};
        q.maxNonOrthogonalityDeg = std::max(q.maxNonOrthogonalityDeg, angle);
        q.maxSkewness = std::max(q.maxSkewness, norm(sub(cf, x)) / norm(d));
        sum += angle;
        ++q.faces;
    }
    q.meanNonOrthogonalityDeg = q.faces == 0 ? 0.0 : sum / static_cast<double>(q.faces);
    return q;
}

/// What P19-MESH-001 recorded, per model, in
/// docs/verification/P19-MESH-001/local-run-2026-10-30/quality-f1-*.log, by its
/// own test-local code and in all three presets identically.
///
/// THE ORACLE FOR THE TWO QUALITY METRICS, and the reason it is written out here:
/// a number taken from a previous milestone's committed evidence cannot have been
/// adjusted to match what this milestone computes.
struct RecordedQuality {
    MeshReferenceModelKind kind;
    std::size_t interiorFaces;
    double maxNonOrthogonalityDeg;
    double meanNonOrthogonalityDeg;
    double maxSkewness;
};

constexpr std::array<RecordedQuality, 8> kRecorded{{
    {MeshReferenceModelKind::Block, 6, 64.09, 47.56, 0.5376},
    {MeshReferenceModelKind::Cylinder, 946, 89.28, 57.89, 4.6077},
    {MeshReferenceModelKind::PlateWithHole, 172, 84.07, 56.70, 6.5099},
    {MeshReferenceModelKind::Tube, 1472, 85.20, 55.27, 3.7138},
    {MeshReferenceModelKind::ThinPlate, 18, 88.81, 66.48, 0.8206},
    {MeshReferenceModelKind::TransformedBase, 18, 66.00, 47.04, 0.8206},
    {MeshReferenceModelKind::TransformedPlaced, 18, 66.00, 47.04, 0.8206},
    {MeshReferenceModelKind::LocalRefinement, 528, 75.58, 31.14, 1.3109},
}};

// ---------------------------------------------------------------------------
// Every structural claim, on every mesh
// ---------------------------------------------------------------------------

/// What `checkStructure` measured while asserting. Returned so a caller can
/// report the margins rather than only assert against them.
struct Structure {
    double worstCellClosure = 0.0;
    double worstDivergenceError = 0.0;
    double worstAreaError = 0.0;
    double worstUnitError = 0.0;
    double worstCentroidError = 0.0;
    double leastBoundaryOutward = std::numeric_limits<double>::infinity();
    double domainClosure = 0.0;
    double worstNonOrthogonalityDeg = 0.0;
    double worstBoundaryNonOrthogonalityDeg = 0.0;
    /// The largest relative error between `meshing::triangleArea` -- a separate
    /// implementation, unsigned -- and |Sf|.
    double worstTriangleAreaError = 0.0;
    /// The largest relative error in `d . n == (h_P + h_N) / 4`, measured from
    /// the two apex heights on EVERY internal face.
    double worstApexError = 0.0;
    std::size_t internalFaces = 0;
    std::size_t boundaryFaces = 0;
};

/// EVERY structural claim of this milestone, asserted on whatever mesh it is
/// given. Called on all eight reference meshes and on the hand-built ones, for
/// the reason P19-TOPO-001's review gave: a claim checked on one model is a claim
/// about that model.
Structure checkStructure(const CfdGeometry& g, const CfdTopology& t, const meshing::Mesh& mesh) {
    Structure out;

    // The geometry is indexed by the topology and bound to the mesh.
    CHECK(g.cells() == t.cells());
    CHECK(g.faces() == t.faces());
    CHECK(g.internalFaces() == t.internalFaces());
    CHECK(g.boundaryFaces() == t.boundaryFaces());
    CHECK(g.describes(mesh));
    CHECK(g.describes(t));
    CHECK(g.mesh() == mesh.stamp());
    // ADR-063's arithmetic identity, restated on the geometry's own counts.
    CHECK(4 * g.cells() == 2 * g.internalFaces() + g.boundaryFaces());
    out.internalFaces = g.internalFaces();
    out.boundaryFaces = g.boundaryFaces();

    const auto pointAt = [&](NodeId id) {
        const meshing::Node* node = mesh.findNode(id);
        REQUIRE(node != nullptr);
        return node->position;
    };
    const auto at = [&](NodeId id) { return si(pointAt(id)); };

    // --- the cells ---------------------------------------------------------
    Volume total{};
    Volume smallest = g.cellVolume(CellIndex::fromValue(0));
    Volume largest = smallest;
    for (std::size_t c = 0; c < g.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        INFO("cell " << c);
        const Volume volume = g.cellVolume(cell);
        // POSITIVE AND FINITE on every cell of every mesh: the checklist's
        // "positive cell volume", and the premise of Decision 5's proof.
        CHECK(isFinite(volume));
        CHECK(volume.si() > 0.0);
        total = total + volume;
        smallest = std::min(smallest, volume);
        largest = std::max(largest, volume);

        const std::optional<Point3D> centroid = g.cellCentroid(cell);
        REQUIRE(centroid.has_value());
        // The centroid recomputed from the MESH's nodes, not read back.
        const meshing::Tetrahedron* tet = mesh.findTetrahedron(t.cellOf(cell));
        REQUIRE(tet != nullptr);
        Vec expected{};
        double scale = 0.0;
        for (const NodeId id : tet->nodes) {
            const Vec p = at(id);
            for (std::size_t i = 0; i < 3; ++i) {
                expected[i] += p[i] / 4.0;
            }
            scale = std::max(scale, norm(p));
        }
        const double centroidError = norm(sub(si(*centroid), expected)) / std::max(scale, 1e-300);
        out.worstCentroidError = std::max(out.worstCentroidError, centroidError);
        CHECK(centroidError < kClosure);

        // THE KEY IDENTITY, with the sign taken from `owner` and not from the
        // order the faces arrive in.
        //
        // THE STORED VECTOR'S VALUE IS READ, not merely its presence. A test-gap
        // review found the value unread anywhere in the suite, so
        // `return AreaVector3D{}` -- or returning cell 0's sum for every cell --
        // survived all 24 cases while the only thing still checked, the
        // dimensionless ratio, stayed below its bound. The sum is re-accumulated
        // here over the same faces, in the same order, with the sign read from
        // `owner`, so the two are BIT-IDENTICAL and can be compared exactly.
        const std::optional<AreaVector3D> sum = g.cellSurfaceVectorSum(cell);
        REQUIRE(sum.has_value());
        AreaVector3D expectedSum{};
        Area expectedScale{};
        for (const FaceIndex face : t.cellFaces(cell)) {
            const AreaVector3D s = g.faceAreaVector(face);
            expectedSum = expectedSum + (t.owner(face) == cell ? s : -s);
            expectedScale = expectedScale + magnitude(s);
        }
        CHECK(sum->x == expectedSum.x);
        CHECK(sum->y == expectedSum.y);
        CHECK(sum->z == expectedSum.z);

        // And the ratio is recomputed rather than only bounded: any rescaling of
        // the denominator -- `4 * scale`, say -- left the residual far below the
        // bound and survived the suite.
        const std::optional<double> closure = g.cellClosure(cell);
        REQUIRE(closure.has_value());
        CHECK(std::isfinite(*closure));
        REQUIRE(expectedScale.si() > 0.0);
        CHECK(*closure == magnitude(expectedSum).si() / expectedScale.si());
        out.worstCellClosure = std::max(out.worstCellClosure, *closure);
        CHECK(*closure < kClosure);

        // AN INDEPENDENT ROUTE TO THE SAME VOLUME: the divergence theorem over
        // this cell's own faces, 3V = sum (C_f - C_P) . Sf_out, which is exact
        // for planar faces and uses the FACE metrics rather than the determinant.
        Volume divergence{};
        std::size_t faceCount = 0;
        for (const FaceIndex face : t.cellFaces(cell)) {
            const AreaVector3D outward =
                t.owner(face) == cell ? g.faceAreaVector(face) : -g.faceAreaVector(face);
            const std::optional<Point3D> centre = g.faceCentroid(face);
            REQUIRE(centre.has_value());
            divergence = divergence + dot(*centre - *centroid, outward);
            ++faceCount;
        }
        CHECK(faceCount == 4);
        const double divergenceError = std::abs(divergence.si() / 3.0 - volume.si()) / volume.si();
        out.worstDivergenceError = std::max(out.worstDivergenceError, divergenceError);
        CHECK(divergenceError < kClosure);
    }
    CHECK_THAT(g.summary().totalVolume.si(), WithinRel(total.si(), kAccumulated));
    CHECK(g.summary().smallestCell == smallest);
    CHECK(g.summary().largestCell == largest);

    // --- the faces ---------------------------------------------------------
    Area internalArea{};
    Area boundaryArea{};
    Vec boundarySum{};
    Length smallestInternalSpan{};
    Length smallestBoundarySpan{};
    bool firstInternalSpan = true;
    bool firstBoundarySpan = true;
    for (std::size_t f = 0; f < g.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        INFO("face " << f);
        const bool internal = f < g.internalFaces();
        CHECK(t.isInternal(face) == internal);

        const AreaVector3D s = g.faceAreaVector(face);
        CHECK(isFinite(s));
        const Vec sv = siOf(s);
        // The area recomputed from the components, not read back from faceArea,
        // which IS magnitude(areaVector) and would assert nothing.
        const double expectedArea = norm(sv);
        CHECK(expectedArea > 0.0);
        const double areaError = std::abs(g.faceArea(face).si() - expectedArea) / expectedArea;
        out.worstAreaError = std::max(out.worstAreaError, areaError);
        CHECK(areaError < kExact);


        const std::optional<Direction3D> normal = g.faceUnitNormal(face);
        REQUIRE(normal.has_value());
        const Vec n{normal->x(), normal->y(), normal->z()};
        const double unitError = std::abs(norm(n) - 1.0);
        out.worstUnitError = std::max(out.worstUnitError, unitError);
        CHECK(unitError < kExact);
        // The normal is PARALLEL to Sf and the same way, not merely perpendicular
        // to the face: n . Sf == |Sf| to rounding.
        CHECK_THAT(dotp(n, sv), WithinRel(expectedArea, kExact));

        // The face centroid recomputed from the mesh's nodes.
        const std::optional<Point3D> centre = g.faceCentroid(face);
        REQUIRE(centre.has_value());
        const std::array<NodeId, 3> nodes = t.faceNodes(face);

        // AND THE AREA AGAINST A SEPARATE IMPLEMENTATION, on every face of every
        // mesh: `meshing::triangleArea` is the unsigned sibling and computes the
        // same area by its own route. This agreement was asserted on one
        // triangle; a review pointed out it costs nothing to assert everywhere.
        const Area unsignedArea =
            meshing::triangleArea(pointAt(nodes[0]), pointAt(nodes[1]), pointAt(nodes[2]));
        const double triangleAreaError = std::abs(unsignedArea.si() - expectedArea) / expectedArea;
        out.worstTriangleAreaError = std::max(out.worstTriangleAreaError, triangleAreaError);
        CHECK(triangleAreaError < kExact);

        Vec expectedCentre{};
        double scale = 0.0;
        for (const NodeId id : nodes) {
            const Vec p = at(id);
            for (std::size_t i = 0; i < 3; ++i) {
                expectedCentre[i] += p[i] / 3.0;
            }
            scale = std::max(scale, norm(p));
        }
        const double centreError = norm(sub(si(*centre), expectedCentre)) / std::max(scale, 1e-300);
        out.worstCentroidError = std::max(out.worstCentroidError, centreError);
        CHECK(centreError < kClosure);

        // d . n > 0 on EVERY face, which is Decision 5 (iv) and (v).
        const Length span = g.orthogonalDistance(face);
        CHECK(isFinite(span));
        CHECK(span.si() > 0.0);
        if (internal) {
            if (firstInternalSpan || span < smallestInternalSpan) {
                smallestInternalSpan = span;
                firstInternalSpan = false;
            }
        } else {
            if (firstBoundarySpan || span < smallestBoundarySpan) {
                smallestBoundarySpan = span;
                firstBoundarySpan = false;
            }
        }

        // THE COSINE IS THE COMPUTED FORM and the angle is the reported one, so
        // both are checked: the cosine is in range (which the bare quotient is
        // NOT -- see the clamp fixture), strictly positive by Decision 5, and the
        // angle is exactly its arccos.
        const std::optional<double> cosine = g.orthogonalityCosine(face);
        REQUIRE(cosine.has_value());
        CHECK(std::isfinite(*cosine));
        CHECK(*cosine > 0.0);
        CHECK(*cosine <= 1.0);
        CHECK(*cosine >= -1.0);

        const std::optional<Angle> angle = g.nonOrthogonality(face);
        REQUIRE(angle.has_value());
        CHECK(isFinite(*angle));
        CHECK(angle->si() >= 0.0);
        // STRICTLY below 90 degrees, by the same proof.
        CHECK(angle->in(units::deg) < 90.0);
        CHECK(angle->si() == arccos(*cosine).si());

        // The relational optionals follow internal/boundary and nothing else.
        const std::optional<Translation3D> d = g.ownerToNeighbour(face);
        const std::optional<double> skew = g.skewness(face);
        CHECK(d.has_value() == internal);
        CHECK(skew.has_value() == internal);
        const Translation3D toFace = g.cellToFace(face);
        CHECK(magnitude(toFace).si() > 0.0);

        if (internal) {
            internalArea = internalArea + g.faceArea(face);
            CHECK(std::isfinite(*skew));
            CHECK(*skew >= 0.0);
            out.worstNonOrthogonalityDeg = std::max(out.worstNonOrthogonalityDeg, angle->in(units::deg));

            // ADR-065's DECISION 5 AS A NUMBER, on EVERY internal face of every
            // mesh rather than on the one face a dedicated case covers:
            // d . n == (h_P + h_N) / 4, with both apex heights measured here from
            // the nodes, and with the SIGNS asserted -- the owner's apex behind
            // the face and the neighbour's in front of it, which is what the
            // winding guarantee means.
            const std::optional<CellIndex> neighbourCell = t.neighbour(face);
            REQUIRE(neighbourCell.has_value());
            const Vec plane = at(nodes[0]);
            const auto apexHeight = [&](CellIndex which) {
                const meshing::Tetrahedron* owned = mesh.findTetrahedron(t.cellOf(which));
                REQUIRE(owned != nullptr);
                for (const NodeId id : owned->nodes) {
                    if (id == nodes[0] || id == nodes[1] || id == nodes[2]) {
                        continue;
                    }
                    return dotp(sub(at(id), plane), n);
                }
                FAIL("the cell has no vertex off the face");
                return 0.0;
            };
            const double ownerHeight = apexHeight(t.owner(face));
            const double neighbourHeight = apexHeight(*neighbourCell);
            CHECK(ownerHeight < 0.0);
            CHECK(neighbourHeight > 0.0);
            const double quarterSum = (std::abs(ownerHeight) + std::abs(neighbourHeight)) / 4.0;
            const double apexError = std::abs(span.si() - quarterSum) / quarterSum;
            out.worstApexError = std::max(out.worstApexError, apexError);
            CHECK(apexError < kClosure);
            // d points from the owner to the neighbour, and the owner is the
            // LOWER cell: ADR-064's order relation, seen through the geometry.
            const std::optional<CellIndex> neighbour = t.neighbour(face);
            REQUIRE(neighbour.has_value());
            CHECK(t.owner(face) < *neighbour);
            CHECK_THAT(norm(siOf(*d)), WithinRel(magnitude(*d).si(), kExact));
        } else {
            boundaryArea = boundaryArea + g.faceArea(face);
            for (std::size_t i = 0; i < 3; ++i) {
                boundarySum[i] += sv[i];
            }
            out.worstBoundaryNonOrthogonalityDeg =
                std::max(out.worstBoundaryNonOrthogonalityDeg, angle->in(units::deg));
            // At a boundary face d IS the cell-to-face vector, so the orthogonal
            // distance is h_P / 4: the other half of Decision 5, on every
            // boundary face rather than on one.
            const meshing::Tetrahedron* owned = mesh.findTetrahedron(t.cellOf(t.owner(face)));
            REQUIRE(owned != nullptr);
            const Vec plane = at(nodes[0]);
            double height = 0.0;
            for (const NodeId id : owned->nodes) {
                if (id == nodes[0] || id == nodes[1] || id == nodes[2]) {
                    continue;
                }
                height = dotp(sub(at(id), plane), n);
            }
            CHECK(height < 0.0);
            const double quarter = std::abs(height) / 4.0;
            const double apexError = std::abs(span.si() - quarter) / quarter;
            out.worstApexError = std::max(out.worstApexError, apexError);
            CHECK(apexError < kClosure);
            // THE OUTWARD NORMAL, checked against the OWNER'S CENTROID rather
            // than against the winding the builder used: the vector from the
            // owner's centroid to the face centroid must have a positive
            // component on the normal, so the normal points out of the fluid.
            const std::optional<BoundaryFaceIndex> boundary = t.boundaryFaceOf(face);
            REQUIRE(boundary.has_value());
            const std::optional<Direction3D> outward = g.boundaryOutwardNormal(*boundary);
            REQUIRE(outward.has_value());
            CHECK(outward->x() == normal->x());
            CHECK(outward->y() == normal->y());
            CHECK(outward->z() == normal->z());
            const double away = dotp(siOf(toFace), n) / std::max(magnitude(toFace).si(), 1e-300);
            out.leastBoundaryOutward = std::min(out.leastBoundaryOutward, away);
            CHECK(away > 0.0);
        }
    }
    CHECK_THAT(g.summary().totalInternalArea.si(), WithinRel(internalArea.si(), kAccumulated));
    CHECK_THAT(g.summary().totalBoundaryArea.si(), WithinRel(boundaryArea.si(), kAccumulated));

    // THE SUMMARY'S EXTREMES ARE COMPARED TO THE EXTREMES, not merely bounded.
    // A review found four of them satisfied by a hard-coded zero, because each
    // was only asserted to be below a bound four orders of magnitude above the
    // quantity. Every comparison here is EXACT, for one of two reasons: the sums
    // and ratios because the test replicates the production accumulation in the
    // same order over the same values, and the two ANGLES because `summarise`
    // takes its maximum over radians while this takes one over degrees, and the
    // conversion is monotone -- so the maximum commutes with it and picks the same
    // element. A review pointed out that "the same order over the same values"
    // was not what made the angles exact.
    CHECK(g.summary().worstNonOrthogonality.in(units::deg) == out.worstNonOrthogonalityDeg);
    CHECK(g.summary().worstBoundaryNonOrthogonality.in(units::deg) == out.worstBoundaryNonOrthogonalityDeg);
    CHECK(g.summary().worstCellClosure == out.worstCellClosure);
    CHECK(g.summary().smallestInternalOrthogonalDistance == smallestInternalSpan);
    CHECK(g.summary().smallestBoundaryOrthogonalDistance == smallestBoundarySpan);
    // AND BOTH POPULATIONS ARE PRESENT AND POSITIVE, which is why they are two
    // fields. NO ORDERING IS ASSERTED: per face the boundary value is the smaller,
    // h_P/4 against (h_P + h_N)/4, but these are minima over DIFFERENT SETS of
    // faces and RM-MESH-02 reverses them. This comment claimed the ordering after
    // the assertion for it had been removed.
    if (g.internalFaces() > 0 && g.boundaryFaces() > 0) {
        CHECK(g.summary().smallestBoundaryOrthogonalDistance.si() > 0.0);
        CHECK(g.summary().smallestInternalOrthogonalDistance.si() > 0.0);
    }
    CHECK(g.summary().worstCellClosure < kClosure);

    // THE WHOLE CLOSED SURFACE sums to zero as well, not only each cell -- and
    // the GEOMETRY'S REPORT of that sum is what is checked, component by
    // component, not only the test's own. Dropping the accumulation from
    // `summarise` left the vector identically zero and every old assertion
    // passing, because the true residual is itself of order eps.
    out.domainClosure = boundaryArea.si() > 0.0 ? norm(boundarySum) / boundaryArea.si() : 0.0;
    CHECK(out.domainClosure < kClosure);
    const AreaVector3D reported = g.summary().boundarySurfaceVectorSum;
    CHECK(reported.x.si() == boundarySum[0]);
    CHECK(reported.y.si() == boundarySum[1]);
    CHECK(reported.z.si() == boundarySum[2]);
    const AreaVector3D independentSum{Area::fromSi(boundarySum[0]), Area::fromSi(boundarySum[1]),
                                      Area::fromSi(boundarySum[2])};
    CHECK(g.summary().domainClosure
          == (boundaryArea.si() > 0.0 ? magnitude(independentSum).si() / boundaryArea.si() : 0.0));
    return out;
}

std::string report(std::string_view label, const Structure& s) {
    return std::format("{}: {} internal + {} boundary faces; closure {:.3e}, divergence {:.3e}, "
                       "domain closure {:.3e}, area {:.3e}, triangleArea {:.3e}, unit {:.3e}, "
                       "centroid {:.3e}, apex {:.3e}, least outward cosine {:.4f}",
                       label, s.internalFaces, s.boundaryFaces, s.worstCellClosure, s.worstDivergenceError,
                       s.domainClosure, s.worstAreaError, s.worstTriangleAreaError, s.worstUnitError,
                       s.worstCentroidError, s.worstApexError, s.leastBoundaryOutward);
}

} // namespace

// ---------------------------------------------------------------------------
// 1. The formulas, against closed forms
// ---------------------------------------------------------------------------

TEST_CASE("CfdGeometry_TheAreaVectorOfATriangleIsTheHalfCrossProduct", "[cfd][geometry]") {
    // A 3-4-5 right triangle in the XY plane, counter-clockwise: area 6, normal
    // +Z. Integers and halves, so the closed form is exact in double precision.
    const Point3D a{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    const Point3D b{Length::fromSi(3.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    const Point3D c{Length::fromSi(0.0), Length::fromSi(4.0), Length::fromSi(0.0)};
    const AreaVector3D s = cfd::triangleAreaVector(a, b, c);
    CHECK(s.x.si() == 0.0);
    CHECK(s.y.si() == 0.0);
    CHECK_THAT(s.z.si(), WithinRel(6.0, kExact));
    CHECK_THAT(magnitude(s).si(), WithinRel(6.0, kExact));
    // And it agrees with meshing::triangleArea, which is the UNSIGNED sibling and
    // a separate implementation.
    CHECK_THAT(meshing::triangleArea(a, b, c).si(), WithinRel(6.0, kExact));

    // REVERSING THE WINDING NEGATES IT, exactly -- which is the whole content of
    // "the sign is the caller's winding". Exact, not approximate: the same
    // products with the operands swapped.
    const AreaVector3D reversed = cfd::triangleAreaVector(a, c, b);
    CHECK(reversed == -s);
    CHECK(magnitude(reversed) == magnitude(s));

    // An equilateral triangle of side 2: area sqrt(3).
    const Point3D p{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    const Point3D q{Length::fromSi(2.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    const Point3D r{Length::fromSi(1.0), Length::fromSi(std::sqrt(3.0)), Length::fromSi(0.0)};
    CHECK_THAT(magnitude(cfd::triangleAreaVector(p, q, r)).si(), WithinRel(std::sqrt(3.0), kExact));

    // A ROTATED COPY of the 3-4-5 triangle presents the same area on a rotated
    // normal, so the formula is not quietly axis-aligned.
    const RigidTransform3D turn = RigidTransform3D::rotation(
        Axis3D{Point3D{}, *Direction3D::fromComponents(1.0, 1.0, 1.0)}, Angle::fromSi(0.7));
    const AreaVector3D turned = cfd::triangleAreaVector(turn.apply(a), turn.apply(b), turn.apply(c));
    CHECK_THAT(magnitude(turned).si(), WithinRel(6.0, kExact));
    CHECK(turned.z.si() != s.z.si());

    // DEGENERATE INPUT GIVES A ZERO VECTOR, which is the correct answer and not
    // an error: three collinear points present no area, and so do three equal
    // ones. This is the path the assembled builder's area refusal guards, reached
    // here where it CAN be reached.
    const Point3D collinear{Length::fromSi(6.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    CHECK(cfd::triangleAreaVector(a, b, collinear) == AreaVector3D{});
    CHECK(cfd::triangleAreaVector(a, a, a) == AreaVector3D{});
    CHECK(magnitude(cfd::triangleAreaVector(a, a, b)).si() == 0.0);
}

TEST_CASE("CfdGeometry_TheCentroidsAreTheMeansOfTheirVertices", "[cfd][geometry]") {
    const Point3D a{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    const Point3D b{Length::fromSi(3.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    const Point3D c{Length::fromSi(0.0), Length::fromSi(6.0), Length::fromSi(0.0)};
    const Point3D d{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(12.0)};

    const Point3D triangle = cfd::triangleCentroid(a, b, c);
    CHECK_THAT(triangle.x.si(), WithinRel(1.0, kExact));
    CHECK_THAT(triangle.y.si(), WithinRel(2.0, kExact));
    CHECK(triangle.z.si() == 0.0);

    const Point3D tet = cfd::tetrahedronCentroid(a, b, c, d);
    CHECK_THAT(tet.x.si(), WithinRel(0.75, kExact));
    CHECK_THAT(tet.y.si(), WithinRel(1.5, kExact));
    CHECK_THAT(tet.z.si(), WithinRel(3.0, kExact));

    // ORDER-INDEPENDENT, unlike the area vector: a centroid has no winding. The
    // two orders that differ are checked, including one that INVERTS the
    // tetrahedron, because the centroid must not notice.
    CHECK(cfd::triangleCentroid(b, c, a) == triangle);
    CHECK(cfd::tetrahedronCentroid(a, b, d, c) == tet);
    CHECK(meshing::signedVolume(a, b, c, d).si() > 0.0);
    CHECK(meshing::signedVolume(a, b, d, c).si() < 0.0);
}

TEST_CASE("CfdGeometry_TheUnitTetrahedronHasItsClosedFormMetrics", "[cfd][geometry]") {
    // EVERY METRIC OF THIS SHAPE IS KNOWN BY HAND, which is why it is the first
    // assembled case: V = 1/6, centroid (1/4, 1/4, 1/4), three right faces of
    // area 1/2 and one face of area sqrt(3)/2.
    const meshing::Mesh mesh = buildMesh(unitTetPoints(), {{0, 1, 2, 3}});
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    INFO((geometry.has_value() ? std::string{} : geometry.error().message));
    REQUIRE(geometry.has_value());

    REQUIRE(geometry->cells() == 1);
    REQUIRE(geometry->faces() == 4);
    CHECK(geometry->internalFaces() == 0);
    CHECK(geometry->boundaryFaces() == 4);

    const CellIndex cell = CellIndex::fromValue(0);
    CHECK_THAT(geometry->cellVolume(cell).si(), WithinRel(1.0 / 6.0, kExact));
    const std::optional<Point3D> centroid = geometry->cellCentroid(cell);
    REQUIRE(centroid.has_value());
    CHECK_THAT(centroid->x.si(), WithinRel(0.25, kExact));
    CHECK_THAT(centroid->y.si(), WithinRel(0.25, kExact));
    CHECK_THAT(centroid->z.si(), WithinRel(0.25, kExact));

    // The four areas, as a multiset: three halves and one sqrt(3)/2. Sorted,
    // because the face ORDER is ADR-064's and this case is about the areas.
    std::vector<double> areas;
    for (std::size_t f = 0; f < geometry->faces(); ++f) {
        areas.push_back(geometry->faceArea(FaceIndex::fromValue(f)).si());
    }
    std::ranges::sort(areas);
    REQUIRE(areas.size() == 4);
    CHECK_THAT(areas[0], WithinRel(0.5, kExact));
    CHECK_THAT(areas[1], WithinRel(0.5, kExact));
    CHECK_THAT(areas[2], WithinRel(0.5, kExact));
    CHECK_THAT(areas[3], WithinRel(std::sqrt(3.0) / 2.0, kExact));
    // Total surface 3/2 + sqrt(3)/2.
    CHECK_THAT(geometry->summary().totalBoundaryArea.si(), WithinRel(1.5 + std::sqrt(3.0) / 2.0, kExact));
    CHECK_THAT(geometry->summary().totalVolume.si(), WithinRel(1.0 / 6.0, kExact));

    // THE ONE INCLINED FACE's unit normal is (1,1,1)/sqrt(3), outward.
    bool sawInclined = false;
    for (std::size_t f = 0; f < geometry->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        if (geometry->faceArea(face).si() < 0.6) {
            continue;
        }
        sawInclined = true;
        const std::optional<Direction3D> n = geometry->faceUnitNormal(face);
        REQUIRE(n.has_value());
        const double third = 1.0 / std::sqrt(3.0);
        CHECK_THAT(n->x(), WithinRel(third, kExact));
        CHECK_THAT(n->y(), WithinRel(third, kExact));
        CHECK_THAT(n->z(), WithinRel(third, kExact));
        // Its centroid is (1/3, 1/3, 1/3), and h_P / 4 is the distance from the
        // cell centroid to the plane: (1/3 - 1/4) * sqrt(3) = sqrt(3)/12.
        CHECK_THAT(geometry->orthogonalDistance(face).si(), WithinRel(std::sqrt(3.0) / 12.0, kExact));
    }
    CHECK(sawInclined);

    const Structure s = checkStructure(*geometry, *topology, mesh);
    WARN(report("unit tetrahedron", s));
}

TEST_CASE("CfdGeometry_TheFourAreaVectorsOfATetrahedronSumToZero", "[cfd][geometry]") {
    // THE MILESTONE'S KEY IDENTITY, on five shapes: a regular tet, a long thin
    // one, one far from the origin, one that is both, and one on coordinates that
    // are not exactly representable.
    //
    // WHAT WAS MEASURED, AND WHY IT IS WORTH SAYING. For the first four the
    // cancellation is BIT-EXACT -- the residual is identically zero, because the
    // four half cross products are formed from the same coordinate differences and
    // the products cancel as the same doubles with opposite signs. That is a fact
    // about these coordinates and not a general guarantee: the fifth case gives
    // 6.8e-17, and the real reference meshes reach 2.03e-16.
    //
    // A RUN OF EXACT ZEROS IS ALSO A WARNING, because "the sum is about zero" is a
    // claim an implementation that returned a zero vector would satisfy perfectly.
    // So the UNSIGNED sum of the same four vectors is asserted to be large: the
    // vectors are real, and only their sum cancels.
    struct Case {
        std::string_view name;
        std::vector<Point3D> points;
    };
    const std::vector<Case> cases{
        {"unit", unitTetPoints()},
        {"sliver 1000:1",
         {Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)},
          Point3D{Length::fromSi(1.0), Length::fromSi(0.0), Length::fromSi(0.0)},
          Point3D{Length::fromSi(0.0), Length::fromSi(1.0), Length::fromSi(0.0)},
          Point3D{Length::fromSi(0.3), Length::fromSi(0.3), Length::fromSi(1.0e-3)}}},
        {"offset by 1e4",
         {Point3D{Length::fromSi(1.0e4), Length::fromSi(1.0e4), Length::fromSi(1.0e4)},
          Point3D{Length::fromSi(1.0e4 + 1.0), Length::fromSi(1.0e4), Length::fromSi(1.0e4)},
          Point3D{Length::fromSi(1.0e4), Length::fromSi(1.0e4 + 1.0), Length::fromSi(1.0e4)},
          Point3D{Length::fromSi(1.0e4), Length::fromSi(1.0e4), Length::fromSi(1.0e4 + 1.0)}}},
        {"sliver offset by 1e3",
         {Point3D{Length::fromSi(1.0e3), Length::fromSi(1.0e3), Length::fromSi(1.0e3)},
          Point3D{Length::fromSi(1.0e3 + 1.0), Length::fromSi(1.0e3), Length::fromSi(1.0e3)},
          Point3D{Length::fromSi(1.0e3), Length::fromSi(1.0e3 + 1.0), Length::fromSi(1.0e3)},
          Point3D{Length::fromSi(1.0e3 + 0.3), Length::fromSi(1.0e3 + 0.3), Length::fromSi(1.0e3 + 1.0e-3)}}},
        // The one case that does NOT cancel exactly: no coordinate here is a
        // dyadic rational, so the products are not the same doubles.
        // The apex lies BELOW the base plane, so the two base vertices are
        // ordered to keep the signed volume positive -- kTetFaces winds outward
        // only of a positively oriented tetrahedron, and the first ordering tried
        // here was negative, which the orientation assertion below caught.
        {"inexact coordinates",
         {Point3D{Length::fromSi(0.1), Length::fromSi(0.2), Length::fromSi(0.3)},
          Point3D{Length::fromSi(0.1), Length::fromSi(2.3), Length::fromSi(0.3)},
          Point3D{Length::fromSi(1.7), Length::fromSi(0.2), Length::fromSi(0.3)},
          Point3D{Length::fromSi(0.37), Length::fromSi(0.41), Length::fromSi(1.0e-4)}}},
    };

    std::string table;
    for (const Case& c : cases) {
        INFO(c.name);
        // The four outward area vectors, from the table directly, so this case does
        // not depend on a topology having been built.
        AreaVector3D sum{};
        AreaVector3D unsignedSum{};
        Area scale{};
        for (const auto& face : kFaces) {
            const AreaVector3D s =
                cfd::triangleAreaVector(c.points[face[0]], c.points[face[1]], c.points[face[2]]);
            sum = sum + s;
            // Every component made positive, so this one cannot cancel at all.
            unsignedSum = unsignedSum + AreaVector3D{abs(s.x), abs(s.y), abs(s.z)};
            scale = scale + magnitude(s);
        }
        CHECK(meshing::signedVolume(c.points[0], c.points[1], c.points[2], c.points[3]).si() > 0.0);
        const double residual = magnitude(sum).si() / scale.si();
        const double unsignedResidual = magnitude(unsignedSum).si() / scale.si();
        table += std::format("  {:<22} sum |Sf| {:.6e} m^2, residual {:.3e}, unsigned {:.3f}\n",
                             c.name, scale.si(), residual, unsignedResidual);
        CHECK(residual < kClosure);
        // THE DISCRIMINATION: the four vectors are substantial, so the
        // cancellation above is the identity holding and not four zeroes summing
        // to zero -- which is what every exact zero in the table would otherwise
        // also be consistent with.
        CHECK(unsignedResidual > 0.5);
        CHECK(scale.si() > 0.0);
    }
    WARN("the four outward area vectors of a tetrahedron cancel\n" + table);
}

TEST_CASE("CfdGeometry_EveryQuantityIsSiWithMetreLengths", "[cfd][geometry]") {
    // THE CHECKLIST'S "SI metre units", asserted where it can go wrong: the mesh
    // is built with MILLIMETRE literals, and every metric must come back in
    // metres, square metres and cubic metres with no factor of 1000 anywhere.
    const meshing::Mesh mesh = buildMesh(
        {Point3D{0_mm, 0_mm, 0_mm}, Point3D{60_mm, 0_mm, 0_mm}, Point3D{0_mm, 60_mm, 0_mm},
         Point3D{0_mm, 0_mm, 60_mm}},
        {{0, 1, 2, 3}});
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(geometry.has_value());

    // V = 60^3 / 6 mm^3 = 36000 mm^3 = 3.6e-5 m^3.
    const Volume volume = geometry->cellVolume(CellIndex::fromValue(0));
    CHECK_THAT(volume.in(units::mm3), WithinRel(36000.0, kExact));
    CHECK_THAT(volume.in(units::m3), WithinRel(3.6e-5, kExact));
    CHECK_THAT(volume.si(), WithinRel(3.6e-5, kExact));
    // `si()` IS the m^3 value: the invariant the units layer exists to hold.
    CHECK(volume.si() == volume.in(units::m3));

    // A right face is 60 x 60 / 2 = 1800 mm^2 = 1.8e-3 m^2.
    std::vector<double> areas;
    for (std::size_t f = 0; f < geometry->faces(); ++f) {
        areas.push_back(geometry->faceArea(FaceIndex::fromValue(f)).si());
    }
    std::ranges::sort(areas);
    CHECK_THAT(areas[0], WithinRel(1.8e-3, kExact));
    CHECK_THAT(Area::fromSi(areas[0]).in(units::mm2), WithinRel(1800.0, kExact));

    // The centroid is at 15 mm on each axis, and `si()` is metres.
    const std::optional<Point3D> centroid = geometry->cellCentroid(CellIndex::fromValue(0));
    REQUIRE(centroid.has_value());
    CHECK_THAT(centroid->x.in(units::mm), WithinRel(15.0, kExact));
    CHECK_THAT(centroid->x.si(), WithinRel(0.015, kExact));

    // THE AREA VECTOR'S COMPONENTS are areas too, not bare numbers, and each one
    // converts like one. The face at index 0 of this single-tet mesh is a right
    // triangle in a coordinate plane, so exactly one component is non-zero and it
    // is the whole 1800 mm^2.
    const AreaVector3D s = geometry->faceAreaVector(FaceIndex::fromValue(0));
    const double biggest = std::max({std::abs(s.x.si()), std::abs(s.y.si()), std::abs(s.z.si())});
    CHECK_THAT(biggest, WithinRel(1.8e-3, kExact));
    CHECK_THAT(Area::fromSi(biggest).in(units::mm2), WithinRel(1800.0, kExact));
    CHECK_THAT(magnitude(s).si(), WithinRel(geometry->faceArea(FaceIndex::fromValue(0)).si(), kExact));

    // THE ORTHOGONAL DISTANCE is a Length, so metres internally and millimetres
    // on request: h_P / 4 with h_P = 60 mm for the three right faces, i.e. 15 mm.
    const Length span = geometry->orthogonalDistance(FaceIndex::fromValue(0));
    CHECK_THAT(span.in(units::mm), WithinRel(15.0, kExact));
    CHECK_THAT(span.si(), WithinRel(0.015, kExact));
    CHECK(span.si() == span.in(units::m));

    // An angle is RADIANS internally and degrees only on request, which is the
    // same rule. The face is chosen so the angle is NOT zero: a zero would make
    // the conversion check 0 against 0, which is what it looked like at first.
    const std::optional<Angle> angle = geometry->nonOrthogonality(FaceIndex::fromValue(0));
    REQUIRE(angle.has_value());
    CHECK(angle->si() > 0.0);
    CHECK_THAT(angle->in(units::deg), WithinRel(angle->si() * 180.0 / std::numbers::pi, kExact));
    CHECK_THAT(angle->in(units::rad), WithinRel(angle->si(), kExact));

    // AND THE TWO DIMENSIONLESS METRICS CARRY NO UNIT AT ALL, which is the other
    // half of the rule: a ratio is a bare double and not a Quantity, because this
    // system returns a plain number for a dimension that cancels completely.
    static_assert(std::is_same_v<decltype(geometry->cellClosure(CellIndex::fromValue(0))),
                                 std::optional<double>>);
    static_assert(std::is_same_v<decltype(geometry->skewness(FaceIndex::fromValue(0))),
                                 std::optional<double>>);
    static_assert(std::is_same_v<decltype(geometry->orthogonalityCosine(FaceIndex::fromValue(0))),
                                 std::optional<double>>);
    static_assert(std::is_same_v<decltype(geometry->cellVolume(CellIndex::fromValue(0))), Volume>);
    static_assert(std::is_same_v<decltype(geometry->faceArea(FaceIndex::fromValue(0))), Area>);
    static_assert(std::is_same_v<decltype(geometry->orthogonalDistance(FaceIndex::fromValue(0))), Length>);
    // The cosine is a pure ratio and in range on this face too.
    const std::optional<double> cosine = geometry->orthogonalityCosine(FaceIndex::fromValue(0));
    REQUIRE(cosine.has_value());
    CHECK(*cosine > 0.0);
    CHECK(*cosine <= 1.0);
}

TEST_CASE("CfdGeometry_ThePointFormulasAreCovariantUnderARigidTransform", "[cfd][geometry]") {
    // THE CHECKLIST'S "rigid-transform covariance", at the point level and
    // exactly: a rotation and a translation leave the volume, the area and every
    // dimensionless metric INVARIANT, and turn the centroid and the area vector
    // COVARIANTLY. A compound rotation that mixes all three axes, so an
    // axis-permutation defect cannot survive it.
    const RigidTransform3D motion =
        RigidTransform3D::translation(Translation3D{7_mm, -13_mm, 23_mm})
            .after(RigidTransform3D::rotation(
                Axis3D{Point3D{3_mm, 1_mm, -2_mm}, *Direction3D::fromComponents(2.0, -1.0, 2.0)},
                Angle::fromSi(1.1)));
    CHECK_FALSE(motion.reversesOrientation());
    CHECK_FALSE(motion.isTranslation());

    const std::vector<Point3D> p = twoCellPoints();
    const std::vector<Point3D> moved{motion.apply(p[0]), motion.apply(p[1]), motion.apply(p[2]),
                                     motion.apply(p[3])};

    // The volume is invariant.
    const Volume before = meshing::signedVolume(p[0], p[1], p[2], p[3]);
    const Volume after = meshing::signedVolume(moved[0], moved[1], moved[2], moved[3]);
    CHECK(before.si() > 0.0);
    CHECK_THAT(after.si(), WithinRel(before.si(), kClosure));

    // The centroid transforms: C(R x) == R C(x).
    const Point3D centroidThenMoved = motion.apply(cfd::tetrahedronCentroid(p[0], p[1], p[2], p[3]));
    const Point3D movedThenCentroid = cfd::tetrahedronCentroid(moved[0], moved[1], moved[2], moved[3]);
    CHECK(distance(centroidThenMoved, movedThenCentroid).si() <
          kClosure * magnitude(Translation3D{centroidThenMoved.x, centroidThenMoved.y, centroidThenMoved.z}).si());

    // The area vector ROTATES: Sf(R x) == A Sf(x), with the matrix and WITHOUT
    // the translation, which is what makes it a vector and not a point.
    const AreaVector3D s = cfd::triangleAreaVector(p[0], p[1], p[2]);
    const AreaVector3D movedS = cfd::triangleAreaVector(moved[0], moved[1], moved[2]);
    const std::array<double, 9>& r = motion.matrix();
    const Vec sv = siOf(s);
    const Vec rotated{r[0] * sv[0] + r[1] * sv[1] + r[2] * sv[2], r[3] * sv[0] + r[4] * sv[1] + r[5] * sv[2],
                      r[6] * sv[0] + r[7] * sv[1] + r[8] * sv[2]};
    CHECK(norm(sub(siOf(movedS), rotated)) < kClosure * norm(sv));
    // And the area itself is invariant, so the rotation did not scale.
    CHECK_THAT(magnitude(movedS).si(), WithinRel(magnitude(s).si(), kClosure));
    // The transform really moved things, so none of the above is trivially true.
    CHECK(norm(sub(siOf(movedS), sv)) > 0.1 * norm(sv));
}

// ---------------------------------------------------------------------------
// 2. The assembly, on hand-built meshes
// ---------------------------------------------------------------------------

TEST_CASE("CfdGeometry_TheOrthogonalDistanceIsTheQuarterSumOfTheTwoApexHeights", "[cfd][geometry]") {
    // ADR-065's DECISION 5, CHECKED AS A NUMBER rather than taken on the ADR's
    // word: across an internal face, d . n == (h_P + h_N) / 4, where h is the
    // perpendicular distance from the face plane to the cell's opposite vertex.
    // This is the identity the whole no-refusal-enum argument rests on, and the
    // two apex heights are measured here from the nodes.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(geometry.has_value());
    REQUIRE(geometry->internalFaces() == 1);

    const FaceIndex face = FaceIndex::fromValue(0);
    const std::optional<Direction3D> n = geometry->faceUnitNormal(face);
    REQUIRE(n.has_value());
    const Vec nv{n->x(), n->y(), n->z()};
    const std::array<NodeId, 3> nodes = topology->faceNodes(face);
    const meshing::Node* plane = mesh.findNode(nodes[0]);
    REQUIRE(plane != nullptr);

    // The signed distance from the face plane to each cell's OPPOSITE vertex: the
    // one of its four nodes that is not on the face.
    const auto apexHeight = [&](CellIndex cell) {
        const meshing::Tetrahedron* tet = mesh.findTetrahedron(topology->cellOf(cell));
        REQUIRE(tet != nullptr);
        for (const NodeId id : tet->nodes) {
            if (id == nodes[0] || id == nodes[1] || id == nodes[2]) {
                continue;
            }
            const meshing::Node* apex = mesh.findNode(id);
            REQUIRE(apex != nullptr);
            return dotp(sub(si(apex->position), si(plane->position)), nv);
        }
        FAIL("the cell has no vertex off the face");
        return 0.0;
    };

    const std::optional<CellIndex> neighbour = topology->neighbour(face);
    REQUIRE(neighbour.has_value());
    const double ownerHeight = apexHeight(topology->owner(face));
    const double neighbourHeight = apexHeight(*neighbour);
    // THE SIGNS ARE THE POINT: the owner's apex is BEHIND the face and the
    // neighbour's is IN FRONT of it. That is what the winding guarantee means,
    // and it is what makes the sum of the two magnitudes positive.
    CHECK(ownerHeight < 0.0);
    CHECK(neighbourHeight > 0.0);
    const double expected = (std::abs(ownerHeight) + std::abs(neighbourHeight)) / 4.0;
    CHECK_THAT(geometry->orthogonalDistance(face).si(), WithinRel(expected, kClosure));

    // And at a boundary face it is h_P / 4, with no neighbour to contribute.
    const FaceIndex boundary = FaceIndex::fromValue(geometry->internalFaces());
    CHECK(topology->isBoundary(boundary));
    CHECK(geometry->orthogonalDistance(boundary).si() > 0.0);
    CHECK_THAT(dot(geometry->cellToFace(boundary), *geometry->faceUnitNormal(boundary)).si(),
               WithinRel(geometry->orthogonalDistance(boundary).si(), kExact));

    const Structure s = checkStructure(*geometry, *topology, mesh);
    WARN(report("two cells sharing a face", s));
}

TEST_CASE("CfdGeometry_TheCellSurfaceVectorSumUsesTheOwnerSignNotTheFaceOrder", "[cfd][geometry]") {
    // THE ONE WAY THE KEY IDENTITY CAN LOOK SATISFIED WHILE EVERY FLUX IS WRONG.
    // ADR-064 stores ONE winding per face, wound out of its OWNER, so the cell on
    // the other side sees the opposite vector. A builder that added +Sf for all
    // four faces of every cell would be right for a cell that owns all four and
    // wrong for every other cell -- and a single-cell mesh could not tell.
    //
    // So: the two-cell mesh, where cell 1 is the NEIGHBOUR of the shared face,
    // and the unsigned sum is computed here as well, to show the signed one is
    // not vacuously zero.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(geometry.has_value());
    REQUIRE(geometry->cells() == 2);

    std::size_t neighbouring = 0;
    for (std::size_t c = 0; c < geometry->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        INFO("cell " << c);
        AreaVector3D unsignedSum{};
        bool isNeighbourSomewhere = false;
        for (const FaceIndex face : topology->cellFaces(cell)) {
            unsignedSum = unsignedSum + geometry->faceAreaVector(face);
            if (topology->owner(face) != cell) {
                isNeighbourSomewhere = true;
            }
        }
        const std::optional<double> closure = geometry->cellClosure(cell);
        REQUIRE(closure.has_value());
        CHECK(*closure < kClosure);
        if (!isNeighbourSomewhere) {
            continue;
        }
        ++neighbouring;
        // THE SIGNED SUM CLOSES AND THE UNSIGNED ONE DOES NOT, by a wide margin:
        // the test is not vacuous.
        Area scale{};
        for (const FaceIndex face : topology->cellFaces(cell)) {
            scale = scale + geometry->faceArea(face);
        }
        const double unsignedResidual = magnitude(unsignedSum).si() / scale.si();
        INFO(std::format("signed residual {:.3e}, unsigned residual {:.3e}", *closure, unsignedResidual));
        CHECK(unsignedResidual > 0.1);
    }
    // Exactly one of the two cells neighbours the shared face, so the branch above
    // really ran.
    CHECK(neighbouring == 1);
}

TEST_CASE("CfdGeometry_SkewnessIsEmptyAtABoundaryFaceAndPresentInside", "[cfd][geometry]") {
    // ADR-065's DECISION 6, asserted on both sides. Skewness is the error in
    // interpolating BETWEEN TWO cell values; at a boundary face there is no second
    // value, and defining it there with d = C_f - C_P would return identically
    // zero -- a number that looks like a measurement. So it is an empty optional,
    // while the non-orthogonality angle, which IS meaningful there, is present.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(geometry.has_value());

    std::size_t internal = 0;
    std::size_t boundary = 0;
    for (std::size_t f = 0; f < geometry->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        INFO("face " << f);
        if (topology->isInternal(face)) {
            ++internal;
            CHECK(geometry->skewness(face).has_value());
            CHECK(geometry->ownerToNeighbour(face).has_value());
        } else {
            ++boundary;
            CHECK_FALSE(geometry->skewness(face).has_value());
            CHECK_FALSE(geometry->ownerToNeighbour(face).has_value());
        }
        // Total either way: the angle and the cell-to-face vector.
        CHECK(geometry->nonOrthogonality(face).has_value());
        CHECK(magnitude(geometry->cellToFace(face)).si() > 0.0);
    }
    CHECK(internal == 1);
    CHECK(boundary == 6);
    // The summary separates the two populations rather than merging them. The one
    // internal face of this fixture is PERFECTLY ORTHOGONAL -- see
    // CfdGeometry_ThePerfectlyOrthogonalFaceIsWhereTheCosineExceedsOne -- so its
    // angle is exactly zero and the boundary faces', which are not, are not.
    CHECK(geometry->summary().worstNonOrthogonality.si() == 0.0);
    CHECK(geometry->summary().worstBoundaryNonOrthogonality.si() > 0.0);
    CHECK(geometry->summary().worstSkewness >= 0.0);
}

TEST_CASE("CfdGeometry_ThePerfectlyOrthogonalFaceIsWhereTheCosineExceedsOne", "[cfd][geometry]") {
    // THE CLAMP IS LOAD-BEARING, AND THIS FIXTURE PROVES IT rather than arguing
    // for it. ADR-065, Decision 7 claims that the BEST possible input is the one
    // that breaks `acos`; the two-cell mesh is that input, and it was already in
    // this suite before the claim was made.
    //
    // Its shared face lies on the plane x + y + z = 10 mm. The owner's centroid
    // is at (2.5, 2.5, 2.5) and the neighbour's at (5, 5, 5), so d is EXACTLY
    // parallel to the face normal and the true angle is exactly zero. Computed in
    // double precision the quotient `d . n / |d|` comes out as
    // 1.000000000000000222 -- one eps ABOVE one -- and `std::acos` of that is NaN.
    // Without the clamp, a NaN non-orthogonality angle would be stored for the
    // most regular interior face a tetrahedral mesh can have.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(geometry.has_value());
    REQUIRE(geometry->internalFaces() == 1);

    const FaceIndex face = FaceIndex::fromValue(0);
    const std::optional<Translation3D> d = geometry->ownerToNeighbour(face);
    REQUIRE(d.has_value());
    // The geometry is the one described above: d parallel to n, and both centroids
    // where the arithmetic above says.
    const std::optional<Point3D> owner = geometry->cellCentroid(topology->owner(face));
    REQUIRE(owner.has_value());
    CHECK_THAT(owner->x.in(units::mm), WithinRel(2.5, kExact));
    CHECK_THAT(owner->y.in(units::mm), WithinRel(2.5, kExact));
    CHECK_THAT(owner->z.in(units::mm), WithinRel(2.5, kExact));

    // THE RAW QUOTIENT, formed exactly as the production code forms it before
    // clamping, and asserted to be STRICTLY GREATER THAN ONE. If a future change
    // made this fixture no longer reach that, this assertion fails and says so,
    // rather than leaving the clamp looking like dead code.
    const double quotient = geometry->orthogonalDistance(face).si() / magnitude(*d).si();
    INFO(std::format("d . n / |d| = {:.20g}, which exceeds 1 by {:.3e}", quotient, quotient - 1.0));
    CHECK(quotient > 1.0);
    CHECK(std::isnan(std::acos(quotient)));

    // AND THE STORED ANGLE IS FINITE AND ZERO, because the clamp is there.
    const std::optional<Angle> angle = geometry->nonOrthogonality(face);
    REQUIRE(angle.has_value());
    CHECK(isFinite(*angle));
    CHECK(angle->si() == 0.0);
    CHECK(geometry->summary().worstNonOrthogonality.si() == 0.0);
    // Nothing else downstream saw a NaN either: the summary and the whole
    // structure are finite.
    CHECK(std::isfinite(geometry->summary().meanNonOrthogonality.si()));
    CHECK(std::isfinite(geometry->summary().worstSkewness));
    WARN(std::format("the perfectly orthogonal interior face: d . n / |d| = {:.20g} "
                     "(1 + {:.3e}); acos of it is {}, the clamped angle is {:.17g} rad",
                     quotient, quotient - 1.0, std::isnan(std::acos(quotient)) ? "NaN" : "finite",
                     angle->si()));
}

TEST_CASE("CfdGeometry_TheAssembledMetricsFollowARigidTransformOfTheWholeMesh", "[cfd][geometry]") {
    // COVARIANCE ON AN ASSEMBLED GEOMETRY, which the point-level case cannot
    // show: the SAME mesh with every node moved, so the topology is identical
    // index for index and the metrics can be compared one to one. A remeshed
    // rotated body gives a different mesh and could only be compared in aggregate
    // (which the RM-MESH-06 pair does, separately).
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const RigidTransform3D motion =
        RigidTransform3D::translation(Translation3D{-31_mm, 17_mm, 5_mm})
            .after(RigidTransform3D::rotation(
                Axis3D{Point3D{2_mm, -3_mm, 1_mm}, *Direction3D::fromComponents(1.0, 2.0, -2.0)},
                Angle::fromSi(2.3)));
    const meshing::Mesh moved = transformed(mesh, motion);
    // A DIFFERENT MESH GENERATION with the same connectivity: the handles match,
    // the stamps do not, and no node is where it was.
    CHECK(moved.nodeCount() == mesh.nodeCount());
    CHECK(moved.elementCount() == mesh.elementCount());
    CHECK_FALSE(moved.stamp() == mesh.stamp());

    const Result<CfdTopology> t0 = cfd::buildCfdTopology(mesh, onePatch(mesh));
    const Result<CfdTopology> t1 = cfd::buildCfdTopology(moved, onePatch(moved));
    REQUIRE(t0.has_value());
    INFO((t1.has_value() ? std::string{} : t1.error().message));
    REQUIRE(t1.has_value());
    const Result<CfdGeometry> g0 = cfd::buildCfdGeometry(mesh, *t0);
    const Result<CfdGeometry> g1 = cfd::buildCfdGeometry(moved, *t1);
    REQUIRE(g0.has_value());
    REQUIRE(g1.has_value());

    // The topologies agree on everything but the stamp, so the indices mean the
    // same thing in both.
    REQUIRE(t0->cells() == t1->cells());
    REQUIRE(t0->faces() == t1->faces());
    REQUIRE(t0->internalFaces() == t1->internalFaces());

    const std::array<double, 9>& r = motion.matrix();
    const auto rotate = [&](const Vec& v) {
        return Vec{r[0] * v[0] + r[1] * v[1] + r[2] * v[2], r[3] * v[0] + r[4] * v[1] + r[5] * v[2],
                   r[6] * v[0] + r[7] * v[1] + r[8] * v[2]};
    };

    double worstVolume = 0.0;
    double worstCentroid = 0.0;
    for (std::size_t c = 0; c < g0->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        INFO("cell " << c);
        // INVARIANT: the volume.
        const double before = g0->cellVolume(cell).si();
        worstVolume = std::max(worstVolume, std::abs(g1->cellVolume(cell).si() - before) / before);
        CHECK_THAT(g1->cellVolume(cell).si(), WithinRel(before, kClosure));
        // COVARIANT: the centroid, which must be the MOVED centroid.
        const std::optional<Point3D> c0 = g0->cellCentroid(cell);
        const std::optional<Point3D> c1 = g1->cellCentroid(cell);
        REQUIRE(c0.has_value());
        REQUIRE(c1.has_value());
        const Point3D expected = motion.apply(*c0);
        const double scale = std::max(norm(si(expected)), 1e-300);
        worstCentroid = std::max(worstCentroid, distance(expected, *c1).si() / scale);
        CHECK(distance(expected, *c1).si() < kClosure * scale);
    }

    double worstArea = 0.0;
    double worstVector = 0.0;
    double worstAngle = 0.0;
    double worstCosine = 0.0;
    double worstSkew = 0.0;
    for (std::size_t f = 0; f < g0->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        INFO("face " << f);
        // INVARIANT: the area, the orthogonal distance, the angle, the skewness.
        const double area = g0->faceArea(face).si();
        worstArea = std::max(worstArea, std::abs(g1->faceArea(face).si() - area) / area);
        CHECK_THAT(g1->faceArea(face).si(), WithinRel(area, kClosure));
        CHECK_THAT(g1->orthogonalDistance(face).si(), WithinRel(g0->orthogonalDistance(face).si(), kClosure));
        // THE COSINE IS THE INVARIANT WORTH ASSERTING TIGHTLY, and the angle is
        // not, because `acos` is ill-conditioned at 1: this fixture's internal
        // face is exactly orthogonal, so its angle is zero and one eps of
        // perturbation in the cosine lifts it to sqrt(2 eps) ~ 2.1e-8 rad.
        //
        // IT IS TAKEN FROM THE ACCESSOR AND NOT FORMED HERE. This divided the
        // orthogonal distance by |cellToFace| on EVERY face -- but on an internal
        // face the distance is taken along `ownerToNeighbour`, so the ratio was
        // 3.0 where the cosine is 1.0: not a cosine of anything. An adversarial
        // review measured it. `orthogonalityCosine` picks the right vector, so
        // the quantity this asserts is now the one the comment describes.
        const std::optional<double> c0 = g0->orthogonalityCosine(face);
        const std::optional<double> c1 = g1->orthogonalityCosine(face);
        REQUIRE(c0.has_value());
        REQUIRE(c1.has_value());
        worstCosine = std::max(worstCosine, std::abs(*c1 - *c0));
        CHECK_THAT(*c1, WithinAbs(*c0, kCosineInvariance));
        const std::optional<Angle> a0 = g0->nonOrthogonality(face);
        const std::optional<Angle> a1 = g1->nonOrthogonality(face);
        REQUIRE(a0.has_value());
        REQUIRE(a1.has_value());
        worstAngle = std::max(worstAngle, std::abs(a1->si() - a0->si()));
        CHECK_THAT(a1->si(), WithinAbs(a0->si(), kAngleNearZero));
        const std::optional<double> s0 = g0->skewness(face);
        const std::optional<double> s1 = g1->skewness(face);
        CHECK(s0.has_value() == s1.has_value());
        if (s0) {
            worstSkew = std::max(worstSkew, std::abs(*s1 - *s0));
            CHECK_THAT(*s1, WithinAbs(*s0, kQualityInvariance));
        }
        // COVARIANT: the area vector rotates with the matrix.
        const Vec expected = rotate(siOf(g0->faceAreaVector(face)));
        worstVector = std::max(worstVector, norm(sub(siOf(g1->faceAreaVector(face)), expected)) / area);
        CHECK(norm(sub(siOf(g1->faceAreaVector(face)), expected)) < kClosure * area);
        // AND IT REALLY MOVED, so none of this is trivially true.
        CHECK(norm(sub(siOf(g1->faceAreaVector(face)), siOf(g0->faceAreaVector(face)))) > 0.1 * area);
    }

    // The aggregate is invariant too.
    CHECK_THAT(g1->summary().totalVolume.si(), WithinRel(g0->summary().totalVolume.si(), kClosure));
    CHECK_THAT(g1->summary().totalBoundaryArea.si(), WithinRel(g0->summary().totalBoundaryArea.si(), kClosure));
    CHECK_THAT(g1->summary().worstSkewness, WithinAbs(g0->summary().worstSkewness, kQualityInvariance));

    WARN(std::format("rigid transform of a whole mesh: volume {:.3e}, centroid {:.3e}, area {:.3e}, "
                     "area vector {:.3e}, cosine {:.3e}, angle {:.3e} rad (sqrt(2 eps) is {:.3e}), "
                     "skewness {:.3e}",
                     worstVolume, worstCentroid, worstArea, worstVector, worstCosine, worstAngle,
                     std::sqrt(2.0 * std::numeric_limits<double>::epsilon()), worstSkew));
    checkStructure(*g1, *t1, moved);
}

TEST_CASE("CfdGeometry_RefusesATopologyDerivedFromAnotherMeshGeneration", "[cfd][geometry]") {
    // THE REFUSAL AN ORDINARY MESH REACHES (ADR-065, Decision 5), and the reason
    // CfdTopology carries a stamp: a topology for one generation says nothing
    // about the next, however equal their counts. The four value guards are
    // reachable too, on inputs no mesher produces, and the three cases above this
    // one reach them; this comment said this was the only one.
    const meshing::Mesh first = buildMesh(twoCellPoints(), kTwoCells);
    const meshing::Mesh second = buildMesh(twoCellPoints(), kTwoCells);
    CHECK_FALSE(first.stamp() == second.stamp());
    // IDENTICAL IN EVERY COUNT AND EVERY COORDINATE, which is exactly why counts
    // are never compared for identity.
    CHECK(first.nodeCount() == second.nodeCount());
    CHECK(first.elementCount() == second.elementCount());

    const Result<CfdTopology> topology = cfd::buildCfdTopology(first, onePatch(first));
    REQUIRE(topology.has_value());
    CHECK(cfd::buildCfdGeometry(first, *topology).has_value());

    const Result<CfdGeometry> refused = cfd::buildCfdGeometry(second, *topology);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("says nothing about"));
    CHECK_THAT(refused.error().message, ContainsSubstring("generation"));
}

TEST_CASE("CfdGeometry_TwoBuildsOfOneMeshAreEqual", "[cfd][geometry]") {
    // DETERMINISM. One mesh, two builds, equal field for field -- which means
    // every stored vector, in order, and the whole summary including the
    // floating-point residuals. Nothing here is keyed by a hash or an address.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> first = cfd::buildCfdGeometry(mesh, *topology);
    const Result<CfdGeometry> second = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    CHECK(first->summary() == second->summary());

    // And a second topology of the same mesh gives the same geometry, so the
    // determinism is the mesh's and not one topology object's.
    const Result<CfdTopology> again = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(again.has_value());
    CHECK(*again == *topology);
    const Result<CfdGeometry> third = cfd::buildCfdGeometry(mesh, *again);
    REQUIRE(third.has_value());
    CHECK(*third == *first);
}

TEST_CASE("CfdGeometry_EqualityDiscriminatesSoDeterminismMeansSomething", "[cfd][geometry]") {
    // The case above is worthless if `==` is lenient. Two geometries of DIFFERENT
    // meshes must compare unequal, and so must their summaries when the shapes
    // differ -- otherwise "two builds are equal" says nothing.
    const meshing::Mesh twoCells = buildMesh(twoCellPoints(), kTwoCells);
    const meshing::Mesh oneCell = buildMesh(unitTetPoints(), {{0, 1, 2, 3}});
    const Result<CfdTopology> t0 = cfd::buildCfdTopology(twoCells, onePatch(twoCells));
    const Result<CfdTopology> t1 = cfd::buildCfdTopology(oneCell, onePatch(oneCell));
    REQUIRE(t0.has_value());
    REQUIRE(t1.has_value());
    const Result<CfdGeometry> g0 = cfd::buildCfdGeometry(twoCells, *t0);
    const Result<CfdGeometry> g1 = cfd::buildCfdGeometry(oneCell, *t1);
    REQUIRE(g0.has_value());
    REQUIRE(g1.has_value());
    CHECK_FALSE(*g0 == *g1);
    CHECK_FALSE(g0->summary() == g1->summary());

    // Two meshes with the SAME shape differ only in their stamp, and that alone
    // must make them unequal: equality is exact representation equality.
    const meshing::Mesh twin = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> t2 = cfd::buildCfdTopology(twin, onePatch(twin));
    REQUIRE(t2.has_value());
    const Result<CfdGeometry> g2 = cfd::buildCfdGeometry(twin, *t2);
    REQUIRE(g2.has_value());
    CHECK_FALSE(*g2 == *g0);
    // ...while the SUMMARIES, which hold no stamp, agree: the numbers are the
    // same and only the identity differs.
    CHECK(g2->summary() == g0->summary());
}

TEST_CASE("CfdGeometry_EveryAccessorIsTotalForAnIndexItDoesNotHold", "[cfd][geometry]") {
    // Every accessor answers for an invalid index and for one past the end: a
    // provably impossible zero where ADR-065's rule gives one, an empty optional
    // where zero would be a legal measurement. No undefined behaviour anywhere.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> g = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(g.has_value());

    for (const CellIndex cell : {CellIndex{}, CellIndex::fromValue(g->cells()), CellIndex::fromValue(999)}) {
        CHECK(g->cellVolume(cell) == Volume{});
        CHECK_FALSE(g->cellCentroid(cell).has_value());
        CHECK_FALSE(g->cellSurfaceVectorSum(cell).has_value());
        CHECK_FALSE(g->cellClosure(cell).has_value());
    }
    for (const FaceIndex face : {FaceIndex{}, FaceIndex::fromValue(g->faces()), FaceIndex::fromValue(999)}) {
        CHECK(g->faceAreaVector(face) == AreaVector3D{});
        CHECK(g->faceArea(face) == Area{});
        CHECK_FALSE(g->faceUnitNormal(face).has_value());
        CHECK_FALSE(g->faceCentroid(face).has_value());
        CHECK_FALSE(g->ownerToNeighbour(face).has_value());
        CHECK(g->cellToFace(face) == Translation3D{});
        CHECK(g->orthogonalDistance(face) == Length{});
        CHECK_FALSE(g->nonOrthogonality(face).has_value());
        CHECK_FALSE(g->skewness(face).has_value());
    }
    for (const BoundaryFaceIndex b :
         {BoundaryFaceIndex{}, BoundaryFaceIndex::fromValue(g->boundaryFaces()), BoundaryFaceIndex::fromValue(999)}) {
        CHECK_FALSE(g->boundaryOutwardNormal(b).has_value());
    }
    // And a zero IS distinguishable, because a held index never gives one.
    CHECK(g->cellVolume(CellIndex::fromValue(0)).si() > 0.0);
    CHECK(g->faceArea(FaceIndex::fromValue(0)).si() > 0.0);
    CHECK(g->orthogonalDistance(FaceIndex::fromValue(0)).si() > 0.0);
    CHECK(magnitude(g->cellToFace(FaceIndex::fromValue(0))).si() > 0.0);
}

TEST_CASE("CfdGeometry_RefusesAZeroAreaFaceOfACellWhoseVolumeComputedPositive", "[cfd][geometry]") {
    // ADR-065's DECISION 5 CLAIMED THIS GUARD WAS UNREACHABLE. It is not, and an
    // adversarial review disproved it on its first try. The claim's error was
    // specific and worth keeping in front of a reader: the proof
    // `V = A h / 3, V > 0 => A > 0` is valid in EXACT arithmetic and says nothing
    // about the COMPUTED values, which is the only thing a guard sees.
    //
    // THE WITNESS. Put three of a tetrahedron's vertices at `0`, `v` and `2v`.
    // The two edge vectors of that face are then `v` and `2v` EXACTLY -- doubling
    // is exact in binary floating point -- so every minor of the cross product is
    // `x*(2y) - y*(2x)`, which is exactly zero whatever `v` is -- in every
    // rounding mode, and at every optimisation level THAT DOES NOT CONTRACT THE
    // PRODUCTS INTO AN FMA. No preset here does; the branch below says so if one
    // ever should.
    //
    // Such a tetrahedron is degenerate, so its exact volume is ALSO zero and its
    // computed volume is pure rounding noise -- whose sign decides whether
    // ADR-064's gate accepts the cell. So BOTH outcomes are asserted below: the
    // test cannot fail because a different build rounded the other way, and it
    // still pins the reachability whenever the gate does accept. On this
    // toolchain the noise is positive and the geometry's refusal is what runs.
    const auto p = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    // Written to full precision because the point IS the exact bit pattern.
    const Point3D apex = p(-0.95665038116011103, -0.09094421627655136, 0.59733515983440943);
    const Point3D origin = p(0.0, 0.0, 0.0);
    const Point3D v = p(-0.83341347712455671, 0.27901295822565619, 0.52736846470935883);
    const Point3D twice = p(2.0 * -0.83341347712455671, 2.0 * 0.27901295822565619, 2.0 * 0.52736846470935883);

    // THE DOUBLING IS EXACT in every rounding mode and under every optimisation.
    CHECK(twice.x.si() == 2.0 * v.x.si());
    CHECK(twice.y.si() == 2.0 * v.y.si());
    CHECK(twice.z.si() == 2.0 * v.z.si());

    // THE ZERO AREA IS EXACT ONLY WITHOUT FUSED MULTIPLY-ADD, and this case said
    // "in every rounding mode, at every optimisation level" first, which an
    // adversarial review disproved. Each minor is `x*(2y) - y*(2x)`; an FMA
    // computes one product exactly and subtracts the ROUNDED other, leaving twice
    // that product's rounding error.
    //
    // Measured on the pinned toolchain, on one minor of this very face, with the
    // operands fed at runtime so constant folding could not settle it:
    //
    //   -O3 (this project's flags)        0                        0 fma emitted
    //   -O3 -march=haswell                1.3450335881681524e-17   1 fma emitted
    //   -O3 -march=haswell -ffp-contract=off  0                    0 fma emitted
    //
    // No preset here sets an FMA-capable `-march`, so the exact branch is the one
    // that runs. It is a BRANCH all the same, for the reason the volume sign is
    // one: a preset that added `-march` should not make this case fail, it should
    // make it say so.
    const AreaVector3D collinear = cfd::triangleAreaVector(origin, v, twice);
    if (!(collinear == AreaVector3D{})) {
        WARN(std::format("this build contracts the cross product into an FMA, so the collinear face "
                         "presents {:.3e} m^2 rather than exactly zero; the zero-area guard is not "
                         "reachable through THIS witness here",
                         magnitude(collinear).si()));
        // Still tiny: the FMA leaves rounding error, not area.
        CHECK(magnitude(collinear).si() < 1.0e-16);
        return;
    }
    CHECK(magnitude(collinear).si() == 0.0);
    CHECK(meshing::triangleArea(origin, v, twice).si() == 0.0);

    // kTetFaces[2] is {1,2,3}, so with the apex first the collinear triple is one
    // of this tetrahedron's four faces.
    const std::vector<Point3D> points{apex, origin, v, twice};
    const Volume volume = meshing::signedVolume(points[0], points[1], points[2], points[3]);
    INFO(std::format("the degenerate cell's computed volume is {:.17g} m^3", volume.si()));

    const meshing::Mesh mesh = buildMesh(points, {{0, 1, 2, 3}});
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));

    if (volume.si() > 0.0) {
        // THE GATE ACCEPTED A DEGENERATE CELL, because its rounding noise was
        // positive. The geometry must refuse, naming the face and its area.
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        const Result<CfdGeometry> refused = cfd::buildCfdGeometry(mesh, *topology);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(refused.error().message, ContainsSubstring("presents an area of"));
        CHECK_THAT(refused.error().message, ContainsSubstring("face "));
        WARN(std::format("the zero-area guard IS reachable: volume {:.17g} m^3 passed ADR-064's gate, "
                         "and the metrics refused with \"{}\"",
                         volume.si(), refused.error().message));
    } else {
        // The noise went the other way and ADR-064's gate refused the cell. The
        // guard is still reachable -- a different coordinate set reaches it -- but
        // not through THIS mesh, and the test says so rather than failing.
        REQUIRE_FALSE(topology.has_value());
        CHECK_THAT(topology.error().message, ContainsSubstring("signed volume"));
        WARN(std::format("on this build the degenerate cell's volume computed as {:.17g} m^3, so ADR-064's "
                         "gate refused it first; the zero area above is still exact",
                         volume.si()));
    }
}

TEST_CASE("CfdGeometry_RefusesACentroidThatOverflowedWhileTheVolumeStayedFinite", "[cfd][geometry]") {
    // THE FOURTH REACHABLE GUARD, found by an adversarial review on its first
    // attempt, and the ONLY ONE THAT IS NOT A ROUNDING ACCIDENT: this one is
    // deterministic, in every rounding mode and at every optimisation level.
    //
    // ADR-065's corrected table first called it unreachable, with the reason "the
    // mesh refuses a non-finite coordinate, and a mean of four finite ones cannot
    // overflow at any coordinate a mesh can hold". The second clause is false.
    // `MeshBuilder` refuses only a NON-FINITE coordinate, so a mesh may hold
    // 1.2e308 four times over -- and `(a + b + c + d) / 4` overflows at the SECOND
    // addition, because 1.2e308 + 1.2e308 exceeds DBL_MAX (1.797e308).
    //
    // The determinant does NOT overflow, which is what lets the configuration past
    // ADR-064's gate: the large magnitude sits on one axis and enters each
    // determinant term only linearly, so it cancels in the edge differences.
    const auto p = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    const std::vector<Point3D> points{p(1.1999999999999999e+308, 0.0, 0.0),
                                      p(1.2000000099999999e+308, 0.0, 0.0),
                                      p(1.1999999999999999e+308, 1.0, 0.0),
                                      p(1.1999999999999999e+308, 0.0, 1.0)};

    // EVERY COORDINATE IS FINITE, so the mesh accepts them...
    for (const Point3D& q : points) {
        CHECK(isFinite(q.x));
        CHECK(isFinite(q.y));
        CHECK(isFinite(q.z));
    }
    // ...and the sum of the four x coordinates is not, deterministically.
    const double sum = points[0].x.si() + points[1].x.si() + points[2].x.si() + points[3].x.si();
    CHECK_FALSE(std::isfinite(sum));
    CHECK(sum > 0.0);
    // The volume IS finite and positive, which is all ADR-064's gate asks.
    const Volume volume = meshing::signedVolume(points[0], points[1], points[2], points[3]);
    INFO(std::format("signed volume {:.17g} m^3, sum of the four x coordinates {}", volume.si(), sum));
    REQUIRE(isFinite(volume));
    REQUIRE(volume.si() > 0.0);
    // And the point-level centroid overflows, which is the mechanism.
    const Point3D centroid = cfd::tetrahedronCentroid(points[0], points[1], points[2], points[3]);
    CHECK_FALSE(isFinite(centroid.x));

    const meshing::Mesh mesh = buildMesh(points, {{0, 1, 2, 3}});
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    CHECK(topology->cells() == 1);

    const Result<CfdGeometry> refused = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("centroid that is not finite"));
    CHECK_THAT(refused.error().message, ContainsSubstring("cell 0"));

    // THE CONTROL, which is what shows the mechanism is the SUM and not the shape:
    // the same configuration at 1e300 overflows nothing and builds normally.
    const std::vector<Point3D> smaller{p(1.0e300, 0.0, 0.0), p(1.00000001e300, 0.0, 0.0),
                                       p(1.0e300, 1.0, 0.0), p(1.0e300, 0.0, 1.0)};
    const double smallerSum =
        smaller[0].x.si() + smaller[1].x.si() + smaller[2].x.si() + smaller[3].x.si();
    CHECK(std::isfinite(smallerSum));
    const meshing::Mesh ok = buildMesh(smaller, {{0, 1, 2, 3}});
    const Result<CfdTopology> okTopology = cfd::buildCfdTopology(ok, onePatch(ok));
    REQUIRE(okTopology.has_value());
    const Result<CfdGeometry> built = cfd::buildCfdGeometry(ok, *okTopology);
    INFO((built.has_value() ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    CHECK(isFinite(built->cellCentroid(CellIndex::fromValue(0))->x));

    WARN(std::format("the centroid guard IS reachable, and deterministically: volume {:.17g} m^3 "
                     "finite and positive, the sum of four finite coordinates not finite, and the "
                     "same shape at 1e300 builds",
                     volume.si()));
}

TEST_CASE("CfdGeometry_RefusesAnOrthogonalDistanceThatLostItsSignToCancellation", "[cfd][geometry]") {
    // THE SECOND GUARD ADR-065 CALLED UNREACHABLE, and the second one an
    // adversarial review reached.
    //
    // WHY NO SINGLE FIXTURE CAN PIN THIS. Exactly, `d . n = (h_P + h_N) / 4` with
    // both heights positive whenever both volumes are positive, so the quantity is
    // ALWAYS positive in exact arithmetic and every witness is a rounding
    // artefact. Drive the two apexes to within `1e-16` of the shared face's plane
    // while leaving them far apart IN the plane, and `d` has components of order
    // the body size while its projection on the normal is of order `1e-18`: the
    // true value sits two orders BELOW the rounding error of differencing the two
    // centroids, so the sign is an accident. It also passes through the unit
    // normal, which is computed with libm, so the sign is not even required to be
    // the same in two builds of this file.
    //
    // So the assertion is on a DETERMINISTIC FAMILY and not on one member: every
    // member that ADR-064's gate accepts must either be refused by the metrics or
    // have a positive orthogonal distance, and at least one of the family must be
    // refused. No random numbers -- the offsets come from a closed form.
    constexpr std::size_t kFamily = 200;
    const auto p = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    // A TILTED shared face, so the normal has three components and `d . n` is a
    // sum of three large products that cancel.
    const Point3D a = p(0.0, 0.0, 0.0);
    const Point3D b = p(0.1, 0.037, 0.011);
    const Point3D c = p(0.021, 0.1, -0.007);
    const Vec ab = sub(si(b), si(a));
    const Vec ac = sub(si(c), si(a));
    const Vec nrm = crossp(ab, ac);
    const double len = norm(nrm);
    REQUIRE(len > 0.0);
    const Vec unit{nrm[0] / len, nrm[1] / len, nrm[2] / len};

    std::size_t accepted = 0;
    std::size_t refused = 0;
    std::size_t built = 0;
    std::size_t distanceRefusals = 0;
    std::size_t skewnessRefusals = 0;
    std::size_t areaRefusals = 0;
    std::string other{};
    double worstDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < kFamily; ++i) {
        const double t = -5.0 + 10.0 * static_cast<double>(i) / static_cast<double>(kFamily - 1);
        const double s = -5.0 + 10.0 * static_cast<double>((i * 97) % kFamily) / static_cast<double>(kFamily - 1);
        // THE HEIGHT SPANS FIVE DECADES ACROSS THE FAMILY, 1e-18 to 1e-14, so the
        // family straddles the transition instead of sitting on one side of it.
        // At 1e-16 only 2 of 200 members lost the sign -- a margin too thin to
        // assert on; at 1e-18 the true projection is two orders BELOW the
        // rounding error of the centroid differencing and the sign is a coin
        // flip. The spread is deliberate and the measured count is reported.
        const double h = 1.0e-18 * std::pow(10.0, static_cast<double>(i % 5));
        const auto apexAt = [&](double sign, double u, double w) {
            const Vec base{si(a)[0] + ab[0] * u + ac[0] * w, si(a)[1] + ab[1] * u + ac[1] * w,
                           si(a)[2] + ab[2] * u + ac[2] * w};
            return p(base[0] + sign * h * unit[0], base[1] + sign * h * unit[1], base[2] + sign * h * unit[2]);
        };
        const std::vector<Point3D> points{a, b, c, apexAt(-1.0, t, s), apexAt(1.0, s, t)};
        // Both orderings tried, because which one is positively oriented depends
        // on the offsets; a cell ADR-064 refuses is simply not part of the family.
        for (const std::vector<std::array<std::size_t, 4>>& tets :
             {std::vector<std::array<std::size_t, 4>>{{0, 1, 2, 3}, {0, 2, 1, 4}},
              std::vector<std::array<std::size_t, 4>>{{0, 2, 1, 3}, {0, 1, 2, 4}}}) {
            const meshing::Mesh mesh = buildMesh(points, tets);
            const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
            if (!topology.has_value() || topology->internalFaces() != 1) {
                continue;
            }
            ++accepted;
            const Result<CfdGeometry> g = cfd::buildCfdGeometry(mesh, *topology);
            if (!g.has_value()) {
                ++refused;
                CHECK(g.error().code == ErrorCode::FailedPrecondition);
                // CLASSIFIED, NOT ASSUMED. Running this family turned up a THIRD
                // guard ADR-065 had called unreachable: at this degeneracy
                // `d . Sf` can compute to zero, so the skewness has no finite
                // value either. The breakdown is counted and reported rather than
                // one kind being assumed.
                const std::string& message = g.error().message;
                if (message.find("orthogonal distance of") != std::string::npos) {
                    ++distanceRefusals;
                } else if (message.find("finite skewness") != std::string::npos) {
                    ++skewnessRefusals;
                } else if (message.find("presents an area of") != std::string::npos) {
                    ++areaRefusals;
                } else {
                    other = message;
                }
                continue;
            }
            ++built;
            // THE INVARIANT THE GUARD PROTECTS: anything that gets through has a
            // positive orthogonal distance, so nothing downstream divides by a
            // non-positive one however degenerate the mesh.
            const Length distance = g->orthogonalDistance(FaceIndex::fromValue(0));
            CHECK(distance.si() > 0.0);
            worstDistance = std::min(worstDistance, distance.si());
        }
    }
    REQUIRE(accepted > 0);
    // At least one of the family loses the sign of the orthogonal distance. This
    // is an assertion about 200 configurations and not about any one of them, for
    // the reason above. Every refusal is one of the three known kinds.
    CHECK(distanceRefusals >= 1);
    CHECK(refused == distanceRefusals + skewnessRefusals + areaRefusals);
    // THE INFO GOES ABOVE THE CHECK, because Catch2's INFO decorates only
    // assertions that FOLLOW it in scope. It sat below, so the message would not
    // have printed for the failure it exists to explain.
    INFO("an unclassified refusal: " << other);
    CHECK(other.empty());
    WARN(std::format("three guards ADR-065 called unreachable ARE reachable. Of {} family members "
                     "ADR-064's gate accepted, the metrics refused {} ({} orthogonal distance, {} "
                     "skewness, {} area) and built {}; the smallest positive distance that got through "
                     "was {:.6e} m",
                     accepted, refused, distanceRefusals, skewnessRefusals, areaRefusals, built,
                     worstDistance));
}

TEST_CASE("CfdGeometry_AnEmptyTopologyYieldsAnEmptyGeometryWithZeroesAndNoRefusal", "[cfd][geometry]") {
    // AN EMPTY TOPOLOGY IS REACHABLE, which P19-TOPO-001 decided deliberately and
    // records in CfdTopology_AnEmptyMeshYieldsAnEmptyTopologyAndIsNotTheContracts-
    // Refusal: `EmptyMesh` is the mesh CONTRACT's refusal (ADR-063), so the
    // topology does not keep a second copy of that rule, and neither does this.
    //
    // So the metrics of nothing must be ZEROES rather than a refusal or a garbage
    // extreme -- in particular `smallestCell` and the two orthogonal distances,
    // which are the two fields whose natural initial value is the FIRST element.
    // That is the defect this case exists to make impossible; nothing else
    // exercised it.
    meshing::MeshBuilder builder;
    const meshing::Mesh mesh = builder.build();
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, {});
    REQUIRE(topology.has_value());
    REQUIRE(topology->cells() == 0);

    const Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    INFO((geometry.has_value() ? std::string{} : geometry.error().message));
    REQUIRE(geometry.has_value());
    CHECK(geometry->cells() == 0);
    CHECK(geometry->faces() == 0);
    CHECK(geometry->internalFaces() == 0);
    CHECK(geometry->boundaryFaces() == 0);
    CHECK(geometry->describes(mesh));
    CHECK(geometry->describes(*topology));

    const cfd::CfdGeometrySummary& s = geometry->summary();
    CHECK(s.totalVolume == Volume{});
    CHECK(s.totalBoundaryArea == Area{});
    CHECK(s.totalInternalArea == Area{});
    CHECK(s.smallestCell == Volume{});
    CHECK(s.largestCell == Volume{});
    CHECK(s.smallestInternalOrthogonalDistance == Length{});
    CHECK(s.smallestBoundaryOrthogonalDistance == Length{});
    CHECK(s.worstNonOrthogonality == Angle{});
    CHECK(s.meanNonOrthogonality == Angle{});
    CHECK(s.worstBoundaryNonOrthogonality == Angle{});
    CHECK(s.worstSkewness == 0.0);
    CHECK(s.worstCellClosure == 0.0);
    CHECK(s.boundarySurfaceVectorSum == AreaVector3D{});
    CHECK(s.domainClosure == 0.0);
    // THE MEAN IS A DIVISION BY THE FACE COUNT, so zero faces is where it would
    // produce a NaN if the count were not guarded.
    CHECK(std::isfinite(s.meanNonOrthogonality.si()));
    CHECK(std::isfinite(s.domainClosure));
    // The whole summary equals a default-constructed one: nothing was fabricated.
    CHECK(s == cfd::CfdGeometrySummary{});
}

TEST_CASE("CfdGeometry_DescribesIsFalseForAnotherGenerationAndAnotherTopology", "[cfd][geometry]") {
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const meshing::Mesh other = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    const Result<CfdTopology> otherTopology = cfd::buildCfdTopology(other, onePatch(other));
    REQUIRE(topology.has_value());
    REQUIRE(otherTopology.has_value());
    const Result<CfdGeometry> g = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(g.has_value());

    CHECK(g->describes(mesh));
    CHECK(g->describes(*topology));
    // STAMPS, NEVER COUNTS: the other mesh and topology agree on every count.
    CHECK_FALSE(g->describes(other));
    CHECK_FALSE(g->describes(*otherTopology));
    CHECK(otherTopology->cells() == topology->cells());
    CHECK(otherTopology->faces() == topology->faces());
    // A default-constructed mesh is not described either, and a default geometry
    // describes nothing at all.
    CHECK_FALSE(g->describes(meshing::Mesh{}));
    CHECK(g->mesh() == mesh.stamp());
}

TEST_CASE("CfdGeometry_AnAnalyticFacePairFixesTheAngleAndTheSkewnessInClosedForm", "[cfd][geometry]") {
    // THE CLOSED FORM THE QUALITY METRICS WERE MISSING. A test-gap review found
    // that the non-orthogonality angle had exactly ONE analytic anchor -- zero, on
    // the perfectly orthogonal fixture -- and that the SKEWNESS HAD NONE. Both
    // were otherwise validated against a second transcription of the same
    // formula, which agrees however wrong the definition is. This fixture fixes
    // both numbers by hand.
    //
    // THE GEOMETRY, every value chosen so the answer is exact. The shared face is
    // the triangle (0,0,0), (1,0,0), (0,1,0) in the z = 0 plane, so its centroid
    // is (1/3, 1/3, 0) and, wound out of the owner, its unit normal is exactly
    // +z:
    //
    //   owner      apex (0, 0, -1)   centroid (1/4, 1/4, -1/4)   V = 1/6
    //   neighbour  apex (0,  4,  2)  centroid (1/4, 5/4,  1/2)   V = 1/3
    //
    //   d = C_N - C_P = (0, 1, 3/4)      |d| = 5/4      d . n = 3/4
    //   cos(theta)  = (3/4) / (5/4) = 3/5                        EXACTLY
    //   the line C_P + t d meets z = 0 at t = 1/3, at x = (1/4, 7/12, 0)
    //   |C_f - x|   = sqrt((1/12)^2 + (1/4)^2) = sqrt(10)/12
    //   skewness    = (sqrt(10)/12) / (5/4) = sqrt(10)/15        EXACTLY
    //
    // A 3-4-5 ratio, so the cosine is 0.6 -- a number this code could not have
    // produced by accident.
    const auto p = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    const std::vector<Point3D> points{p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0, 0, -1), p(0, 4, 2)};
    // The owner is added FIRST, so it is the lower ElementId and ADR-063's first
    // claimant. Both orderings are positively oriented, which the topology would
    // refuse otherwise.
    const std::vector<std::array<std::size_t, 4>> tets{{0, 2, 1, 3}, {0, 1, 2, 4}};
    CHECK(meshing::signedVolume(points[0], points[2], points[1], points[3]).si() > 0.0);
    CHECK(meshing::signedVolume(points[0], points[1], points[2], points[4]).si() > 0.0);

    const meshing::Mesh mesh = buildMesh(points, tets);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    const Result<CfdGeometry> g = cfd::buildCfdGeometry(mesh, *topology);
    INFO((g.has_value() ? std::string{} : g.error().message));
    REQUIRE(g.has_value());
    REQUIRE(g->cells() == 2);
    REQUIRE(g->internalFaces() == 1);
    CHECK(g->boundaryFaces() == 6);

    const FaceIndex face = FaceIndex::fromValue(0);
    const CellIndex owner = topology->owner(face);
    const std::optional<CellIndex> neighbour = topology->neighbour(face);
    REQUIRE(neighbour.has_value());

    // The two volumes, in closed form.
    CHECK_THAT(g->cellVolume(owner).si(), WithinRel(1.0 / 6.0, kExact));
    CHECK_THAT(g->cellVolume(*neighbour).si(), WithinRel(1.0 / 3.0, kExact));

    // The two centroids, in closed form -- every component a dyadic rational, so
    // these are exact equalities and not approximations.
    const std::optional<Point3D> cp = g->cellCentroid(owner);
    const std::optional<Point3D> cn = g->cellCentroid(*neighbour);
    REQUIRE(cp.has_value());
    REQUIRE(cn.has_value());
    CHECK(cp->x.si() == 0.25);
    CHECK(cp->y.si() == 0.25);
    CHECK(cp->z.si() == -0.25);
    CHECK(cn->x.si() == 0.25);
    CHECK(cn->y.si() == 1.25);
    CHECK(cn->z.si() == 0.5);

    // The normal is exactly +z, so the winding is the owner's.
    const std::optional<Direction3D> n = g->faceUnitNormal(face);
    REQUIRE(n.has_value());
    CHECK(n->x() == 0.0);
    CHECK(n->y() == 0.0);
    CHECK(n->z() == 1.0);

    // d, its length, and the orthogonal distance: all exact.
    const std::optional<Translation3D> d = g->ownerToNeighbour(face);
    REQUIRE(d.has_value());
    CHECK(d->x.si() == 0.0);
    CHECK(d->y.si() == 1.0);
    CHECK(d->z.si() == 0.75);
    CHECK(magnitude(*d).si() == 1.25);
    CHECK(g->orthogonalDistance(face).si() == 0.75);

    // THE ANGLE, against the closed form rather than against another
    // transcription of the formula.
    const std::optional<double> cosine = g->orthogonalityCosine(face);
    REQUIRE(cosine.has_value());
    CHECK_THAT(*cosine, WithinRel(3.0 / 5.0, kExact));
    const std::optional<Angle> angle = g->nonOrthogonality(face);
    REQUIRE(angle.has_value());
    CHECK_THAT(angle->si(), WithinRel(std::acos(0.6), kExact));
    CHECK_THAT(angle->in(units::deg), WithinRel(53.13010235415598, kExact));

    // THE SKEWNESS, against the closed form. This is the metric's only analytic
    // anchor in the milestone.
    const std::optional<double> skew = g->skewness(face);
    REQUIRE(skew.has_value());
    CHECK_THAT(*skew, WithinRel(std::sqrt(10.0) / 15.0, kExact));
    CHECK_THAT(*skew, WithinRel(0.21081851067789195, kExact));

    const Structure s = checkStructure(*g, *topology, mesh);
    WARN(report("analytic face pair (cos 3/5, skew sqrt(10)/15)", s));
}

TEST_CASE("CfdGeometry_TheRecommendedConsumerCosineIsInRangeOnEveryFace", "[cfd][geometry]") {
    // THE DEFECT A REVIEW FOUND IN ADR-065's OWN ADVICE. Decision 7 told a
    // consumer to divide the orthogonal distance by |d| rather than take the
    // cosine of the angle, for the conditioning -- and did not say that the
    // quotient CAN EXCEED ONE, which the clamp fixture in this very file proves.
    // A non-orthogonal correction following that advice would hand an
    // out-of-range cosine to acos and get a NaN on the best mesh in the suite.
    //
    // The division is now `orthogonalityCosine`, with the clamp inside it, and
    // this case is the recommended consumer path exercised: in range, usable by
    // acos, and on every face of every reference mesh rather than one.
    std::size_t faces = 0;
    std::size_t clamped = 0;
    double worstRaw = 0.0;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid prepared(info.kind);
        const CfdGeometry& g = *prepared.geometry;
        for (std::size_t f = 0; f < g.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const std::optional<double> cosine = g.orthogonalityCosine(face);
            REQUIRE(cosine.has_value());
            // IN RANGE, so acos accepts it and cannot return NaN.
            CHECK(*cosine >= -1.0);
            CHECK(*cosine <= 1.0);
            CHECK_FALSE(std::isnan(std::acos(*cosine)));
            CHECK(std::isfinite(std::acos(*cosine)));
            // And it is the cosine of the reported angle.
            const std::optional<Angle> angle = g.nonOrthogonality(face);
            REQUIRE(angle.has_value());
            CHECK_THAT(std::cos(angle->si()), WithinAbs(*cosine, 1e-15));

            // THE RAW QUOTIENT, formed as a caller following the retracted advice
            // would, so the hazard is measured rather than asserted away.
            const Translation3D span = g.ownerToNeighbour(face).value_or(g.cellToFace(face));
            const double raw = g.orthogonalDistance(face).si() / magnitude(span).si();
            worstRaw = std::max(worstRaw, raw);
            if (raw > 1.0) {
                ++clamped;
            }
            ++faces;
        }
    }
    CHECK(faces > 0);
    WARN(std::format("the recommended cosine over {} faces of 8 reference meshes: in range everywhere; "
                     "the UNCLAMPED quotient exceeded 1 on {} of them, worst {:.20g}",
                     faces, clamped, worstRaw));
}

TEST_CASE("CfdGeometry_TheBoundarySkewnessDecision6RejectedWouldBeIdenticallyZero", "[cfd][geometry]") {
    // ADR-065's DECISION 6 argued that defining skewness at a boundary face with
    // d = C_f - C_P would put the crossing point exactly at C_f and return
    // identically zero -- "a number that looks like a measurement". A review
    // pointed out that the argument is trivially demonstrable and was not
    // demonstrated, so it is computed here instead of asserted in prose.
    //
    // With d = C_f - C_P the line C_P + t d passes through C_f at t = 1, and the
    // face plane contains C_f, so the crossing IS C_f and |C_f - x| is zero for
    // every boundary face of every mesh. That is why the accessor is empty there.
    std::size_t boundaryFaces = 0;
    double worst = 0.0;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid prepared(info.kind);
        const CfdGeometry& g = *prepared.geometry;
        for (std::size_t b = 0; b < g.boundaryFaces(); ++b) {
            const FaceIndex face = prepared.topology->faceOf(BoundaryFaceIndex::fromValue(b));
            // The accessor is empty, which is the decision.
            CHECK_FALSE(g.skewness(face).has_value());

            // And the rejected definition, computed here: lambda is 1 and the
            // crossing point is the face centroid, so the skewness is 0.
            const Translation3D d = g.cellToFace(face);
            const AreaVector3D s = g.faceAreaVector(face);
            const std::optional<Point3D> owner = g.cellCentroid(prepared.topology->owner(face));
            const std::optional<Point3D> centre = g.faceCentroid(face);
            REQUIRE(owner.has_value());
            REQUIRE(centre.has_value());
            const double along = dot(d, s).si();
            REQUIRE(along != 0.0);
            const double lambda = dot(*centre - *owner, s).si() / along;
            const Point3D crossing = *owner + d * lambda;
            const double rejected = distance(*centre, crossing).si() / magnitude(d).si();
            worst = std::max(worst, rejected);
            ++boundaryFaces;
        }
    }
    CHECK(boundaryFaces > 0);
    // Identically zero to rounding: the definition measures nothing there.
    CHECK(worst < 1e-15);
    WARN(std::format("Decision 6's rejected boundary skewness over {} boundary faces: worst {:.3e} "
                     "(identically zero, which is why the accessor is empty)",
                     boundaryFaces, worst));
}

// ---------------------------------------------------------------------------
// 3. The reference meshes, and the independent oracles
// ---------------------------------------------------------------------------

TEST_CASE("CfdGeometry_EveryP16ReferenceMeshYieldsGeometry", "[cfd][geometry]") {
    // THE MILESTONE'S CENTRAL CLAIM, measured on every body P16 qualified, with
    // EVERY structural claim run on EVERY one of them. RM-MESH-08, the open
    // profile, has no body and so no mesh.
    std::size_t meshed = 0;
    std::string table;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid prepared(info.kind);
        ++meshed;
        const Structure s = checkStructure(*prepared.geometry, *prepared.topology, prepared.mesh());
        table += report(std::format("{} {}", info.id, info.name), s) + "\n";

        // DETERMINISM ON EVERY REFERENCE MESH, not only on a hand-built two-cell
        // one: a second build of the same mesh and topology must be equal field
        // for field, including every stored vector and the whole summary with its
        // floating-point residuals. A review pointed out the sweep was generic
        // and the claim was being made on one small mesh.
        const Result<CfdGeometry> again = cfd::buildCfdGeometry(prepared.mesh(), *prepared.topology);
        REQUIRE(again.has_value());
        CHECK(*again == *prepared.geometry);
        CHECK(again->summary() == prepared.geometry->summary());
        // The geometry is bound to the same generation the contract and the
        // topology were proved for, through the staged gate.
        CHECK(prepared.geometry->mesh() == prepared.contract->mesh());
        CHECK(prepared.geometry->geometry() == prepared.model->geometryRevision());
        CHECK(prepared.geometry->cells() == prepared.contract->census().cells);
        CHECK(prepared.geometry->internalFaces() == prepared.contract->census().interiorFaces);
        CHECK(prepared.geometry->boundaryFaces() == prepared.contract->census().boundaryFaces);
    }
    // Eight bodies; a model added to the suite is covered without an edit here.
    CHECK(meshed == 8);
    WARN("P19-GEOM-001 structure on every P16 reference mesh\n" + table);
}

TEST_CASE("CfdGeometry_TheTotalVolumeAgreesWithTwoOraclesAndTwoPlumbingChecks", "[cfd][geometry]") {
    // FOUR ROUTES TO THE SAME NUMBER, of which TWO are independent of the
    // formula under test and two are plumbing checks -- see this file's header
    // for which is which, and why calling all four independent was wrong.
    //
    //   tetrahedralVolume()  PLUMBING: the same determinants, summed in P16's order
    //   boundaryVolume()     PLUMBING: already gated against the above by P16
    //   cadVolume()          INDEPENDENT: the kernel on the exact solid
    //   analyticVolumeMm3    INDEPENDENT: the catalog's closed form
    //
    // The two plumbing routes must agree to accumulation on every model. The two
    // independent ones agree TIGHTLY only for the PLANAR bodies, whose facets tile
    // the exact solid; for the three curved ones the DIRECTION of the difference
    // is the prediction the catalog makes, and it is asserted instead of a
    // tolerance. `mine` is compared against the kernel's volume as well, which a
    // review found was never done -- only `cad` against `analytic`, a leg that
    // involves nothing from the code under test.
    std::string table;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid prepared(info.kind);
        const meshing::VolumeMesh& volume = prepared.volumeMesh();
        const double mine = prepared.geometry->summary().totalVolume.in(units::mm3);
        const double tets = volume.tetrahedralVolume().in(units::mm3);
        const double boundary = volume.boundaryVolume().in(units::mm3);
        const double cad = volume.cadVolume().in(units::mm3);
        const double analytic = info.analyticVolumeMm3;

        // THE TWO MESH ROUTES, on every model.
        CHECK_THAT(mine, WithinRel(tets, kAccumulated));
        CHECK_THAT(mine, WithinRel(boundary, kAccumulated));
        // The kernel's volume against the closed form: the suite's own leg, and
        // independent of the mesh entirely.
        CHECK_THAT(cad, WithinRel(analytic, kCadVolume));

        const bool planar = info.kind == MeshReferenceModelKind::Block
                            || info.kind == MeshReferenceModelKind::ThinPlate
                            || info.kind == MeshReferenceModelKind::TransformedBase
                            || info.kind == MeshReferenceModelKind::TransformedPlaced
                            || info.kind == MeshReferenceModelKind::LocalRefinement;
        const double error = std::abs(mine - analytic) / analytic;
        table += std::format("  {:<28} mine {:.10g}  tets {:.10g}  boundary {:.10g}  cad {:.10g}  "
                             "analytic {:.10g}  rel {:.3e} {}\n",
                             info.name, mine, tets, boundary, cad, analytic, error, planar ? "planar" : "curved");
        if (planar) {
            // PLANAR: the facets tile the exact solid, so only accumulation
            // separates the sum from the closed form -- and from the KERNEL's
            // own integral of that solid, which is the independent oracle.
            CHECK_THAT(mine, WithinRel(analytic, kAccumulated));
            CHECK_THAT(mine, WithinRel(cad, kAccumulated));
        } else if (info.kind == MeshReferenceModelKind::PlateWithHole) {
            // THE HOLE IS A CHORD POLYGON INSCRIBED IN THE CIRCLE, so it removes
            // LESS material than the true circle and the mesh is LARGER. The
            // opposite direction from the other two curved models, which is a
            // check no single-sided tolerance could make.
            CHECK(mine > analytic);
            CHECK(mine > cad);
            CHECK(error < 0.05);
        } else {
            // CURVED AND CONVEX: chords cut inside the arc, so the mesh cannot
            // exceed the solid.
            CHECK(mine < analytic);
            CHECK(mine < cad);
            CHECK(error < 0.05);
        }
    }
    WARN("P19-GEOM-001 total volume against four independent oracles (mm^3)\n" + table);
}

TEST_CASE("CfdGeometry_TheTotalBoundaryAreaMatchesTheClosedFormOfEveryPlanarBody", "[cfd][geometry]") {
    // THE SURFACE, which no oracle above touches: a volume can be right while
    // every face area is wrong. For a rectangular box of edges a, b, c the closed
    // form is 2(ab + bc + ca), read from the model's OWN PARAMETERS and never from
    // anything BetterCAD computed. Four of the eight bodies are boxes.
    struct Box {
        MeshReferenceModelKind kind;
        std::string_view id;
        std::array<std::string_view, 3> parameters;
    };
    const std::array<Box, 4> boxes{{
        {MeshReferenceModelKind::Block, "RM-MESH-01", {"block_a", "block_b", "block_c"}},
        {MeshReferenceModelKind::ThinPlate, "RM-MESH-05", {"thin_l", "thin_w", "thin_t"}},
        {MeshReferenceModelKind::TransformedBase, "RM-MESH-06", {"base_a", "base_b", "base_c"}},
        {MeshReferenceModelKind::LocalRefinement, "RM-MESH-07", {"local_a", "local_b", "local_c"}},
    }};

    std::string table;
    for (const Box& box : boxes) {
        INFO(box.id);
        PreparedFluid prepared(box.kind);
        const double a = mm(prepared.fluid.model.document(), box.parameters[0]);
        const double b = mm(prepared.fluid.model.document(), box.parameters[1]);
        const double c = mm(prepared.fluid.model.document(), box.parameters[2]);
        // Asymmetric, so an axis swap could not pass either check.
        CHECK(a != b);
        CHECK(b != c);
        const double expectedArea = 2.0 * (a * b + b * c + c * a);
        const double expectedVolume = a * b * c;
        const double area = prepared.geometry->summary().totalBoundaryArea.in(units::mm2);
        const double error = std::abs(area - expectedArea) / expectedArea;
        table += std::format("  {} {:>6.1f} x {:>5.1f} x {:>5.1f} mm: area {:.10g} vs {:.10g} mm^2 "
                             "(rel {:.3e}), volume rel {:.3e}\n",
                             box.id, a, b, c, area, expectedArea, error,
                             std::abs(prepared.geometry->summary().totalVolume.in(units::mm3) - expectedVolume)
                                 / expectedVolume);
        CHECK_THAT(area, WithinRel(expectedArea, kAccumulated));
        CHECK_THAT(prepared.geometry->summary().totalVolume.in(units::mm3),
                   WithinRel(expectedVolume, kAccumulated));
        // The internal faces carry area too, and a lot more of it than the
        // boundary on a refined body: a sanity bound, not a closed form.
        CHECK(prepared.geometry->summary().totalInternalArea.si() > 0.0);
    }
    WARN("P19-GEOM-001 boundary area against the closed form 2(ab + bc + ca)\n" + table);
}

TEST_CASE("CfdGeometry_TheQualityMetricsAgreeWithThoseP19Mesh001Recorded", "[cfd][geometry]") {
    // THE CROSS-MILESTONE ORACLE. P19-MESH-001 measured the non-orthogonality
    // angle and the skewness in its own test-local code and RECORDED the numbers
    // in its evidence, with a note that this milestone "will compute them in
    // production and must agree". The expected values here are transcribed from
    // that committed log, so they cannot have been adjusted to match.
    //
    // The interior face count is checked too: it is the same number ADR-064's
    // census reports, from a third code path.
    std::string table;
    std::size_t covered = 0;
    for (const RecordedQuality& expected : kRecorded) {
        const auto info = std::ranges::find_if(bettercad::reference::kMeshReferenceModels,
                                               [&](const auto& m) { return m.kind == expected.kind; });
        REQUIRE(info != bettercad::reference::kMeshReferenceModels.end());
        INFO(info->id << " " << info->name);
        PreparedFluid prepared(info->kind);
        const cfd::CfdGeometrySummary& s = prepared.geometry->summary();

        // The count, from the topology.
        CHECK(prepared.topology->internalFaces() == expected.interiorFaces);

        const double maxDeg = s.worstNonOrthogonality.in(units::deg);
        const double meanDeg = s.meanNonOrthogonality.in(units::deg);
        table += std::format("  {:<28} faces {:>5}  max {:>6.2f} / {:>6.2f}  mean {:>6.2f} / {:>6.2f}  "
                             "skew {:.4f} / {:.4f}\n",
                             info->name, prepared.topology->internalFaces(), maxDeg,
                             expected.maxNonOrthogonalityDeg, meanDeg, expected.meanNonOrthogonalityDeg,
                             s.worstSkewness, expected.maxSkewness);
        CHECK_THAT(maxDeg, WithinAbs(expected.maxNonOrthogonalityDeg, kRecordedAngleDeg));
        CHECK_THAT(meanDeg, WithinAbs(expected.meanNonOrthogonalityDeg, kRecordedAngleDeg));
        CHECK_THAT(s.worstSkewness, WithinAbs(expected.maxSkewness, kRecordedSkewness));

        // AND A SECOND, INDEPENDENT COMPUTATION in this file, from the definitions,
        // which bounds the two code paths against each other much more tightly
        // than the two decimal places of the record allow.
        const Quality independent = independentQuality(*prepared.topology, prepared.mesh());
        CHECK(independent.faces == prepared.topology->internalFaces());
        CHECK_THAT(independent.maxNonOrthogonalityDeg, WithinRel(maxDeg, kAgreement));
        CHECK_THAT(independent.meanNonOrthogonalityDeg, WithinRel(meanDeg, kAgreement));
        CHECK_THAT(independent.maxSkewness, WithinRel(s.worstSkewness, kAgreement));
        ++covered;
    }
    // Eight meshes, all covered: the table above is not a subset.
    CHECK(covered == 8);
    CHECK(kRecorded.size() == 8);
    WARN("P19-GEOM-001 quality against what P19-MESH-001 recorded (mine / recorded)\n" + table);
}

TEST_CASE("CfdGeometry_TheTransformedReferenceBodyPairHasInvariantAggregates", "[cfd][geometry]") {
    // RM-MESH-06: the SAME block at the origin and under a compound rotation and
    // translation, meshed separately. The two meshes are DIFFERENT -- the mesher
    // works in the body's frame, so a rotated body gives different tetrahedra --
    // so this cannot be compared face for face. What must be invariant is the
    // volume, the surface area and the dimensionless quality metrics, and
    // P19-MESH-001 already recorded those as identical to two decimal places.
    PreparedFluid base(MeshReferenceModelKind::TransformedBase);
    PreparedFluid placed(MeshReferenceModelKind::TransformedPlaced);

    const cfd::CfdGeometrySummary& b = base.geometry->summary();
    const cfd::CfdGeometrySummary& p = placed.geometry->summary();

    // THE MESH REALLY MOVED, so the invariance below is not trivial.
    const std::optional<meshing::MeshBounds> baseBounds = base.mesh().bounds();
    const std::optional<meshing::MeshBounds> placedBounds = placed.mesh().bounds();
    REQUIRE(baseBounds.has_value());
    REQUIRE(placedBounds.has_value());
    CHECK_FALSE(baseBounds->min == placedBounds->min);

    CHECK_THAT(p.totalVolume.in(units::mm3), WithinRel(b.totalVolume.in(units::mm3), kAccumulated));
    CHECK_THAT(p.totalBoundaryArea.in(units::mm2), WithinRel(b.totalBoundaryArea.in(units::mm2), kAccumulated));
    CHECK_THAT(p.worstNonOrthogonality.in(units::deg),
               WithinAbs(b.worstNonOrthogonality.in(units::deg), kRecordedAngleDeg));
    CHECK_THAT(p.worstSkewness, WithinAbs(b.worstSkewness, kRecordedSkewness));
    // Same cell count, same face counts: the same discretisation, moved.
    CHECK(placed.geometry->cells() == base.geometry->cells());
    CHECK(placed.geometry->internalFaces() == base.geometry->internalFaces());
    CHECK(placed.geometry->boundaryFaces() == base.geometry->boundaryFaces());

    WARN(std::format("RM-MESH-06 invariance: volume {:.10g} / {:.10g} mm^3, area {:.10g} / {:.10g} mm^2, "
                     "worst angle {:.4f} / {:.4f} deg, skewness {:.6f} / {:.6f}",
                     b.totalVolume.in(units::mm3), p.totalVolume.in(units::mm3),
                     b.totalBoundaryArea.in(units::mm2), p.totalBoundaryArea.in(units::mm2),
                     b.worstNonOrthogonality.in(units::deg), p.worstNonOrthogonality.in(units::deg),
                     b.worstSkewness, p.worstSkewness));
}

TEST_CASE("CfdGeometry_TheAssembledMetricsFollowARigidTransformOfEveryReferenceMesh", "[cfd][geometry]") {
    // COVARIANCE FIELD FOR FIELD ON ALL EIGHT, not on one hand-built two-cell
    // mesh. A test-gap review pointed out that `transformed()` is entirely
    // generic and that asserting the milestone's covariance item on a single
    // small fixture repeats exactly the defect P19-TOPO-001's review found --
    // a claim checked on one model is a claim about that model.
    //
    // The transform is applied NODE BY NODE, so the connectivity, the handles and
    // therefore the whole index assignment are identical and the metrics can be
    // compared one to one. Re-meshing a rotated body gives different tetrahedra
    // and can only be compared in aggregate, which the RM-MESH-06 pair does.
    const RigidTransform3D motion =
        RigidTransform3D::translation(Translation3D{-41_mm, 23_mm, -11_mm})
            .after(RigidTransform3D::rotation(
                Axis3D{Point3D{5_mm, -7_mm, 3_mm}, *Direction3D::fromComponents(-2.0, 1.0, 2.0)},
                Angle::fromSi(1.9)));
    REQUIRE_FALSE(motion.reversesOrientation());
    const std::array<double, 9>& r = motion.matrix();
    const auto rotate = [&](const Vec& v) {
        return Vec{r[0] * v[0] + r[1] * v[1] + r[2] * v[2], r[3] * v[0] + r[4] * v[1] + r[5] * v[2],
                   r[6] * v[0] + r[7] * v[1] + r[8] * v[2]};
    };

    std::size_t swept = 0;
    std::string table;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid prepared(info.kind);
        const meshing::Mesh moved = transformed(prepared.mesh(), motion);
        CHECK(moved.nodeCount() == prepared.mesh().nodeCount());
        CHECK(moved.elementCount() == prepared.mesh().elementCount());

        // THE MODEL'S OWN PATCH PARTITION, REUSED, and not `onePatch`. The
        // boundary range is ordered by (patch, owner, key), so collapsing eight
        // CAD faces into one patch REORDERS it and face n then means a different
        // face in the two geometries -- which is what the first version of this
        // sweep did, and the orthogonal distances disagreed by a factor of three.
        //
        // Reusing the real mapping requires the rebuilt mesh to carry the same
        // element handles, which it does only if the original's are gap-free;
        // MeshBuilder has no explicit-handle overload for an element, so that is
        // asserted rather than assumed.
        REQUIRE(moved.triangles().size() == prepared.mesh().triangles().size());
        for (std::size_t k = 0; k < moved.triangles().size(); ++k) {
            REQUIRE(moved.triangles()[k].id == prepared.mesh().triangles()[k].id);
        }
        REQUIRE(moved.tetrahedra().size() == prepared.mesh().tetrahedra().size());
        for (std::size_t k = 0; k < moved.tetrahedra().size(); ++k) {
            REQUIRE(moved.tetrahedra()[k].id == prepared.mesh().tetrahedra()[k].id);
        }
        const Result<CfdTopology> movedTopology =
            cfd::buildCfdTopology(moved, prepared.model->map().faces());
        INFO((movedTopology.has_value() ? std::string{} : movedTopology.error().message));
        REQUIRE(movedTopology.has_value());
        const Result<CfdGeometry> movedGeometry = cfd::buildCfdGeometry(moved, *movedTopology);
        INFO((movedGeometry.has_value() ? std::string{} : movedGeometry.error().message));
        REQUIRE(movedGeometry.has_value());

        // The index assignment survived the move, so index n means the same cell
        // and the same face in both.
        REQUIRE(movedTopology->cells() == prepared.topology->cells());
        REQUIRE(movedTopology->faces() == prepared.topology->faces());
        REQUIRE(movedTopology->internalFaces() == prepared.topology->internalFaces());

        const CfdGeometry& before = *prepared.geometry;
        const CfdGeometry& after = *movedGeometry;
        double worstVolume = 0.0;
        double worstCentroid = 0.0;
        double worstVector = 0.0;
        double worstCosine = 0.0;
        double worstSkew = 0.0;
        for (std::size_t c = 0; c < before.cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            INFO("cell " << c);
            // INVARIANT: the volume.
            const double volume = before.cellVolume(cell).si();
            worstVolume = std::max(worstVolume, std::abs(after.cellVolume(cell).si() - volume) / volume);
            CHECK_THAT(after.cellVolume(cell).si(), WithinRel(volume, kClosure));
            // COVARIANT: the centroid moves with the transform.
            const std::optional<Point3D> c0 = before.cellCentroid(cell);
            const std::optional<Point3D> c1 = after.cellCentroid(cell);
            REQUIRE(c0.has_value());
            REQUIRE(c1.has_value());
            const Point3D expected = motion.apply(*c0);
            const double scale = std::max(norm(si(expected)), 1e-300);
            worstCentroid = std::max(worstCentroid, distance(expected, *c1).si() / scale);
            CHECK(distance(expected, *c1).si() < kClosure * scale);
        }
        for (std::size_t f = 0; f < before.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            INFO("face " << f);
            // INVARIANT: the area, the orthogonal distance, the cosine, the
            // skewness. COVARIANT: the area vector rotates with the matrix.
            const double area = before.faceArea(face).si();
            CHECK_THAT(after.faceArea(face).si(), WithinRel(area, kClosure));
            CHECK_THAT(after.orthogonalDistance(face).si(),
                       WithinRel(before.orthogonalDistance(face).si(), kClosure));
            const std::optional<double> k0 = before.orthogonalityCosine(face);
            const std::optional<double> k1 = after.orthogonalityCosine(face);
            REQUIRE(k0.has_value());
            REQUIRE(k1.has_value());
            worstCosine = std::max(worstCosine, std::abs(*k1 - *k0));
            CHECK_THAT(*k1, WithinAbs(*k0, kCosineInvariance));
            const std::optional<double> s0 = before.skewness(face);
            const std::optional<double> s1 = after.skewness(face);
            CHECK(s0.has_value() == s1.has_value());
            if (s0) {
                worstSkew = std::max(worstSkew, std::abs(*s1 - *s0));
                CHECK_THAT(*s1, WithinAbs(*s0, kQualityInvariance));
            }
            const Vec expected = rotate(siOf(before.faceAreaVector(face)));
            worstVector = std::max(worstVector, norm(sub(siOf(after.faceAreaVector(face)), expected)) / area);
            CHECK(norm(sub(siOf(after.faceAreaVector(face)), expected)) < kClosure * area);
        }
        // AND THE AGGREGATES are invariant too.
        CHECK_THAT(after.summary().totalVolume.si(), WithinRel(before.summary().totalVolume.si(), kClosure));
        CHECK_THAT(after.summary().totalBoundaryArea.si(),
                   WithinRel(before.summary().totalBoundaryArea.si(), kClosure));
        CHECK_THAT(after.summary().worstSkewness, WithinAbs(before.summary().worstSkewness, kQualityInvariance));
        ++swept;
        table += std::format("  {:<28} {:>5} cells {:>5} faces: volume {:.3e}, centroid {:.3e}, "
                             "area vector {:.3e}, cosine {:.3e}, skewness {:.3e}\n",
                             info.name, before.cells(), before.faces(), worstVolume, worstCentroid,
                             worstVector, worstCosine, worstSkew);
    }
    CHECK(swept == 8);
    WARN("P19-GEOM-001 covariance, field for field, on every reference mesh\n" + table);
}

TEST_CASE("CfdGeometry_TheStagedGateNamesTheControlAndTheModelsRevision", "[cfd][geometry]") {
    // `requireCfdGeometry` adds what the plain builder cannot: the meshing control
    // in a refusal, and the MODEL's geometry revision on the result rather than
    // the topology's copy of it.
    PreparedFluid prepared(MeshReferenceModelKind::Block);
    CHECK(prepared.geometry->geometry() == prepared.model->geometryRevision());
    CHECK(prepared.geometry->geometry().isValid());

    // The plain builder on the same mesh and topology gives the same metrics...
    const Result<CfdGeometry> plain = cfd::buildCfdGeometry(prepared.mesh(), *prepared.topology);
    REQUIRE(plain.has_value());
    CHECK_THAT(plain->summary().totalVolume.si(),
               WithinRel(prepared.geometry->summary().totalVolume.si(), kExact));
    CHECK(plain->cells() == prepared.geometry->cells());

    // ...and a topology from ANOTHER generation is refused by the staged gate with
    // the control named, which is the diagnostic the plain builder cannot give.
    const meshing::Mesh other = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> foreign = cfd::buildCfdTopology(other, onePatch(other));
    REQUIRE(foreign.has_value());
    const Result<CfdGeometry> refused = cfd::requireCfdGeometry(*prepared.model, *foreign);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message,
               ContainsSubstring(std::format("meshing control {}", prepared.model->control().value())));
    CHECK_THAT(refused.error().message, ContainsSubstring("says nothing"));
}

TEST_CASE("CfdGeometry_TheSummaryIsMeasuredAndJudgesNothing", "[cfd][geometry]") {
    // ADR-065's DECISION 7 and P19-MESH-001's owner finding F1: the eight P16
    // reference meshes are POOR finite-volume meshes, and this milestone refuses
    // none of them. The numbers are recorded here so that P19-VALID-001 sets its
    // threshold against measured values rather than guesses.
    std::string table;
    double worstAngle = 0.0;
    double worstSkew = 0.0;
    double worstClosure = 0.0;
    double smallestCellRatio = 1.0;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        PreparedFluid prepared(info.kind);
        const cfd::CfdGeometrySummary& s = prepared.geometry->summary();
        // EVERY MESH IS ACCEPTED, however bad its metrics.
        CHECK(prepared.geometry->cells() > 0);
        const double ratio = s.smallestCell.si() / s.largestCell.si();
        worstAngle = std::max(worstAngle, s.worstNonOrthogonality.in(units::deg));
        worstSkew = std::max(worstSkew, s.worstSkewness);
        worstClosure = std::max(worstClosure, s.worstCellClosure);
        smallestCellRatio = std::min(smallestCellRatio, ratio);
        table += std::format("  {:<28} cells {:>5}  V [{:.4e}, {:.4e}] mm^3 ratio {:.3e}  d_min int {:.4e} bdy {:.4e} mm  "
                             "angle {:>6.2f} deg (bdy {:>6.2f})  skew {:>8.4f}  closure {:.3e}  domain {:.3e}\n",
                             info.name, prepared.geometry->cells(),
                             s.smallestCell.in(units::mm3), s.largestCell.in(units::mm3), ratio,
                             s.smallestInternalOrthogonalDistance.in(units::mm),
                             s.smallestBoundaryOrthogonalDistance.in(units::mm),
                             s.worstNonOrthogonality.in(units::deg),
                             s.worstBoundaryNonOrthogonality.in(units::deg), s.worstSkewness, s.worstCellClosure,
                             s.domainClosure);
        // Measured, never judged: the only bounds asserted are the ones the proof
        // in ADR-065 Decision 5 makes, not an acceptance threshold.
        CHECK(s.smallestCell.si() > 0.0);
        CHECK(s.smallestInternalOrthogonalDistance.si() > 0.0);
        CHECK(s.smallestBoundaryOrthogonalDistance.si() > 0.0);
        // NO ORDERING IS ASSERTED BETWEEN THEM, and a first version of this case
        // wrongly asserted that the boundary minimum is always the smaller. Per
        // FACE it is -- h_P/4 against (h_P + h_N)/4 -- but the two minima are
        // taken over DIFFERENT SETS of faces, so a mesh whose worst sliver is
        // interior reverses them. RM-MESH-02 does exactly that: 0.141 mm
        // internal against 0.189 mm boundary. That the two differ at all is
        // the reason they are two fields; which is smaller is a fact about the
        // mesh, and this milestone judges no mesh.
        CHECK(s.worstNonOrthogonality.in(units::deg) < 90.0);
        CHECK(s.worstBoundaryNonOrthogonality.in(units::deg) < 90.0);
        CHECK(std::isfinite(s.worstSkewness));
    }
    // F1 is a real finding and this records that it is still true: these meshes
    // are near-degenerate for a finite-volume method, and nothing here refuses
    // them.
    CHECK(worstAngle > 80.0);
    CHECK(worstSkew > 1.0);
    WARN(std::format("P19-GEOM-001 measured, judged nowhere (owner finding F1 stands)\n{}"
                     "  worst over the suite: angle {:.2f} deg, skewness {:.4f}, closure {:.3e}, "
                     "smallest/largest cell {:.3e}\n",
                     table, worstAngle, worstSkew, worstClosure, smallestCellRatio));
}
