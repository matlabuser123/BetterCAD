// P19-MESH-001: the CFD mesh contract and the gate that proves a P16 mesh meets it
// (ADR-063).
//
// TWO KINDS OF EVIDENCE. Real P16 meshes -- every reference model P16 qualified --
// must satisfy the contract, and the gate's census must agree with an INDEPENDENT
// census (tests/reference/MeshTestSupport.hpp's `faceIncidence`, written for
// P16's own qualification). And every refusal must be shown on a mesh built by
// hand to fail it, because P16 cannot be made to produce one: a VolumeMesh cannot
// be built by hand, which is why `inspectCfdMesh` takes a plain Mesh.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using bettercad::reference::MeshReferenceModelKind;
using Catch::Matchers::ContainsSubstring;
using cfd::CfdMeshContract;
using cfd::CfdMeshInspection;
using cfd::CfdMeshProblem;
using meshing::NodeId;

namespace {

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// A mapping report that is complete for @p facets boundary facets.
meshing::GeometryMeshMappingReport completeMapping(std::size_t facets) {
    meshing::GeometryMeshMappingReport report;
    report.cadFaceCount = 1;
    report.namedFaceCount = 1;
    report.boundaryFacetCount = facets;
    report.mappedFacetCount = facets;
    return report;
}

/// How a hand-built mesh's boundary triangles are to be spoiled.
struct Spoil {
    bool omitFirst = false;          // leave one boundary face uncovered
    bool omitLast = false;           // leave another one uncovered
    bool reverseFirst = false;       // wind one triangle into its cell
    bool duplicateFirst = false;     // a second triangle on one face, wound INTO the cell
    bool coverAnInteriorFace = false; // a triangle on a face >= 2 cells share, wound into the first
    bool addAStrayTriangle = false;  // a triangle that is no face of any cell
};

/// Builds a mesh from @p points and @p tets, then adds a boundary triangle for
/// every face exactly one tet uses, wound outward of that tet by the local table
/// {0,2,1},{0,1,3},{1,2,3},{0,3,2} -- the convention ADR-063 fixes -- and then
/// spoils the boundary as @p spoil asks. A fixture generator, not an oracle: the
/// orientation check is proved against P16's own meshes, and a reversed triangle.
meshing::Mesh buildMesh(const std::vector<Point3D>& points, const std::vector<std::array<std::size_t, 4>>& tets,
                        Spoil spoil = {}) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> nodes;
    for (const Point3D& p : points) {
        nodes.push_back(require(builder.addNode(p)));
    }
    static constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};
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
    // The spoiled triangles that must be refused for OWNERSHIP are wound INTO
    // their cell, so that a gate which forgot to stop at the ownership refusal
    // would report a second problem and fail the test's exact expectation.
    std::size_t boundary = 0;
    for (const auto& [key, use] : uses) {
        boundary += use.first == 1 ? 1U : 0U;
    }
    bool first = true;
    std::size_t seen = 0;
    for (const auto& [key, use] : uses) {
        const auto& [count, w] = use;
        if (count >= 2 && spoil.coverAnInteriorFace) {
            REQUIRE(builder.addTriangle({nodes[w[0]], nodes[w[2]], nodes[w[1]]}, kRegion));
            spoil.coverAnInteriorFace = false;
        }
        if (count != 1) {
            continue;
        }
        const bool last = ++seen == boundary;
        if ((first && spoil.omitFirst) || (last && spoil.omitLast)) {
            first = false;
            continue;
        }
        std::array<NodeId, 3> triangle{nodes[w[0]], nodes[w[1]], nodes[w[2]]};
        if (first && spoil.reverseFirst) {
            std::swap(triangle[1], triangle[2]);
        }
        REQUIRE(builder.addTriangle(triangle, kRegion));
        if (first && spoil.duplicateFirst) {
            REQUIRE(builder.addTriangle({triangle[0], triangle[2], triangle[1]}, kRegion));
        }
        first = false;
    }
    if (spoil.addAStrayTriangle) {
        // Three nodes of the first cell that are not one of its faces' combinations
        // is impossible for a tet, so a fresh node makes the stray face.
        const NodeId stray = require(builder.addNode(Point3D{5_mm, 5_mm, 5_mm}));
        REQUIRE(builder.addTriangle({nodes[0], nodes[1], stray}, kRegion));
    }
    return builder.build();
}

/// Two cells sharing one face: A = (1,2,3,4), B = (2,3,4,5), both positive.
std::vector<Point3D> twoCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}, Point3D{10_mm, 10_mm, 10_mm}, Point3D{20_mm, 20_mm, 20_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};

CfdMeshInspection inspect(const meshing::Mesh& mesh) {
    return cfd::inspectCfdMesh(mesh, completeMapping(mesh.triangles().size()));
}

std::vector<CfdMeshProblem> problemsOf(const CfdMeshInspection& inspection) {
    std::vector<CfdMeshProblem> problems;
    for (const cfd::CfdMeshFinding& finding : inspection.findings) {
        problems.push_back(finding.problem);
    }
    return problems;
}

void assignWater(Document& document) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> id = features::createMaterial(document, "CfdWater", water);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// A P16 reference model (or any document holding a control named "Mesh"),
/// meshed, with water assigned and the CFD input prepared.
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

/// A 40 x 30 x 10 mm block with a 12 mm hole drilled through it, and a control
/// named "Mesh" on the drilled body: P16's own case of an UNNAMED face, because
/// `cutHole` names a hole's flat faces and not its cylindrical wall
/// (MapBored_DrilledHoleWallIsMappedInReverseAndCarriesNoName).
Document boredDocument() {
    Document document{"CfdBored"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    (void)addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude =
        features::ExtrudeFeature::create("Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    const ObjectId solid = require(document.addObject(std::move(*extrude)));
    const features::HoleDefinition hole{
        .target = FeatureId::fromValue(solid.value()),
        .face = geometry::planeSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitZ().reversed()),
        .center = Point2D{20_mm, 15_mm},
        .extent = geometry::HoleExtent::Through,
        .diameter = 12_mm};
    auto bore = features::HoleFeature::create("Bore", hole);
    REQUIRE(bore.has_value());
    const ObjectId drilled = require(document.addObject(std::move(*bore)));
    meshing::MeshControlDefinition control;
    control.body = drilled;
    control.mesh.sizing.globalTargetSize = 8_mm;
    control.mesh.surface.linearDeflection = Length::fromSi(1e-4);
    control.quality = meshing::reportOnlyThresholds();
    auto created = meshing::MeshControl::create("Mesh", control);
    REQUIRE(created.has_value());
    (void)require(document.addObject(std::move(*created)));
    return document;
}

} // namespace

// ---------------------------------------------------------------------------
// P16's own meshes satisfy the contract
// ---------------------------------------------------------------------------

TEST_CASE("CfdMesh_EveryP16ReferenceMeshSatisfiesTheContract", "[cfd][mesh]") {
    // THE ANSWER TO THE CRITICAL QUESTION, measured: every reference body P16
    // qualified -- a block, a cylinder, a plate with a hole, a tube, a thin plate, a
    // block at the origin and the same block rotated and moved, and a locally
    // refined block -- meshes to a valid finite-volume mesh with no adapter. The
    // census agrees with an independent one. RM-MESH-08, the open profile, has no
    // body and so no mesh to inspect; P16 refuses it, and ADR-060's input boundary
    // refuses a body that failed to regenerate before this gate is reachable
    // (CfdAnalysisBoundaryTests).
    std::size_t meshed = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == MeshReferenceModelKind::OpenProfile);
            continue;
        }
        const MeshReferenceModelKind kind = info.kind;
        INFO(info.id << " " << info.name);
        MeshedFluid fluid(kind);
        const Result<cfd::CfdModel> model = fluid.prepare();
        ++meshed;
        INFO((model.has_value() ? std::string{} : model.error().message));
        REQUIRE(model.has_value());

        const Result<CfdMeshContract> contract = cfd::requireCfdMesh(*model);
        INFO((contract.has_value() ? std::string{} : contract.error().message));
        REQUIRE(contract.has_value());
        CHECK_FALSE(cfd::cfdMeshProblem(*model).has_value());

        const cfd::CfdMeshCensus& census = contract->census();
        const meshing::Mesh& mesh = model->mesh().mesh();
        CHECK(census.cells == mesh.tetrahedra().size());
        CHECK(census.nonManifoldFaces == 0);
        CHECK(census.components == 1);
        CHECK(census.boundaryFaces == census.boundaryTriangles);
        CHECK(census.boundaryTriangles == mesh.triangles().size());
        CHECK(4 * census.cells == 2 * census.interiorFaces + census.boundaryFaces);

        // An independent census, written for P16's qualification, agrees.
        const meshref::Incidence incidence = meshref::faceIncidence(mesh);
        CHECK(incidence.consistent());
        CHECK(census.interiorFaces == incidence.interiorFaces);
        CHECK(census.boundaryFaces == incidence.boundaryFaces);

        // Patches are the CAD faces of the body; the unnamed ones are counted.
        CHECK(contract->patches() == model->map().faces().size());
        CHECK(contract->unnamedPatches() == model->map().report().unnamedFaceCount);
        CHECK(contract->describes(mesh));
        CHECK(contract->mesh() == mesh.stamp());
        CHECK(contract->geometry() == model->geometryRevision());
        // The gate relies on the CfdModel to pair the map with the mesh (ADR-060)
        // and does not re-ask it; this is that reliance, asserted.
        CHECK(mesh.owns(model->map().meshStamp()));

        WARN(std::format("{} {}: {} cells, {} interior faces, {} boundary faces, {} patches ({} unnamed)", info.id,
                         info.name, census.cells, census.interiorFaces, census.boundaryFaces, contract->patches(),
                         contract->unnamedPatches()));
    }
    // Eight bodies meshed; a model added to the set is covered without an edit here.
    CHECK(meshed == 8);
}

namespace {

using Vec = std::array<double, 3>;

Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dotp(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec crossp(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double norm(const Vec& a) { return std::sqrt(dotp(a, a)); }

/// The two ADR-063 quality measures over every interior face, computed here from
/// the definitions alone (P19-GEOM-001 will compute them in production and must
/// agree): the non-orthogonality angle between the face area vector S (wound out of
/// the owner) and d = C_N - C_P, and the skewness |C_f - x| / |d|, where x is where
/// the line C_P -> C_N crosses the face plane.
struct Quality {
    double maxNonOrthogonalityDeg = 0.0;
    double meanNonOrthogonalityDeg = 0.0;
    double maxSkewness = 0.0;
    double minDotSd = 0.0; // min over faces of S.d / (|S||d|)
    std::size_t interiorFaces = 0;
};

Quality measure(const meshing::Mesh& mesh) {
    std::vector<Vec> centre;
    std::map<std::array<NodeId, 3>, std::pair<std::size_t, std::array<NodeId, 3>>> owner;
    std::vector<std::pair<std::size_t, std::size_t>> pairs; // owner, neighbour
    std::vector<std::array<NodeId, 3>> windings;
    const auto at = [&](NodeId id) {
        const Point3D& p = mesh.findNode(id)->position;
        return Vec{p.x.si(), p.y.si(), p.z.si()};
    };
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        Vec c{};
        for (const NodeId id : tet.nodes) {
            const Vec p = at(id);
            for (std::size_t i = 0; i < 3; ++i) {
                c[i] += p[i] / 4.0;
            }
        }
        const std::size_t cell = centre.size();
        centre.push_back(c);
        for (const auto& local : cfd::kTetFaces) {
            const std::array<NodeId, 3> wound{tet.nodes[local[0]], tet.nodes[local[1]], tet.nodes[local[2]]};
            std::array<NodeId, 3> key = wound;
            std::ranges::sort(key);
            if (const auto it = owner.find(key); it == owner.end()) {
                owner.emplace(key, std::pair{cell, wound});
            } else {
                pairs.emplace_back(it->second.first, cell);
                windings.push_back(it->second.second);
            }
        }
    }
    Quality q;
    q.minDotSd = 1.0;
    double sum = 0.0;
    for (std::size_t f = 0; f < pairs.size(); ++f) {
        const Vec a = at(windings[f][0]);
        const Vec b = at(windings[f][1]);
        const Vec c = at(windings[f][2]);
        const Vec s = crossp(sub(b, a), sub(c, a)); // 2 S; only its direction is used
        const Vec cf{(a[0] + b[0] + c[0]) / 3.0, (a[1] + b[1] + c[1]) / 3.0, (a[2] + b[2] + c[2]) / 3.0};
        const Vec& cp = centre[pairs[f].first];
        const Vec d = sub(centre[pairs[f].second], cp);
        const double cosine = dotp(s, d) / (norm(s) * norm(d));
        const double angle = std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        const double lambda = dotp(sub(cf, cp), s) / dotp(d, s);
        const Vec x{cp[0] + lambda * d[0], cp[1] + lambda * d[1], cp[2] + lambda * d[2]};
        q.maxNonOrthogonalityDeg = std::max(q.maxNonOrthogonalityDeg, angle);
        q.maxSkewness = std::max(q.maxSkewness, norm(sub(cf, x)) / norm(d));
        q.minDotSd = std::min(q.minDotSd, cosine);
        sum += angle;
    }
    q.interiorFaces = pairs.size();
    q.meanNonOrthogonalityDeg = pairs.empty() ? 0.0 : sum / static_cast<double>(pairs.size());
    return q;
}

} // namespace

TEST_CASE("CfdMesh_TheReferenceMeshQualityIsMeasuredNotJudged", "[cfd][mesh][quality]") {
    // ADR-058 left the tetrahedral finite-volume quality question to be MEASURED.
    // Measured here on every P16 reference mesh, and recorded; JUDGED nowhere in
    // this milestone, because an acceptance threshold is P19-VALID-001's decision.
    //
    // One bound IS a consequence of the contract, and is asserted: across an
    // interior face of two positive cells that traverse it in OPPOSITE directions
    // (InconsistentFaceOrientation refuses otherwise), the owner's centroid lies on
    // the inner side of the face plane and the neighbour's on the outer side, so
    // S . d > 0 and the non-orthogonality is strictly below 90 degrees on every
    // face (ADR-063 §6).
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        MeshedFluid fluid(info.kind);
        const Result<cfd::CfdModel> model = fluid.prepare();
        REQUIRE(model.has_value());
        REQUIRE(cfd::requireCfdMesh(*model).has_value());
        const Quality q = measure(model->mesh().mesh());
        CHECK(q.interiorFaces > 0);
        CHECK(q.minDotSd > 0.0);
        CHECK(q.maxNonOrthogonalityDeg < 90.0);
        CHECK(std::isfinite(q.maxSkewness));
        WARN(std::format("{} {}: {} interior faces, non-orthogonality max {:.2f} deg mean {:.2f} deg, skewness max "
                         "{:.4f}",
                         info.id, info.name, q.interiorFaces, q.maxNonOrthogonalityDeg, q.meanNonOrthogonalityDeg,
                         q.maxSkewness));
    }
}

TEST_CASE("CfdMesh_TheFaceTableIsTheOneP16WindsItsBoundaryWith", "[cfd][mesh]") {
    // P16 holds two file-local copies of the face table (VolumeMesh.cpp and
    // GeometryMeshMap.cpp); CFD's is the third, and public. They must agree face
    // for face and node for node, or a boundary triangle P16 emitted and the face
    // CFD derives would disagree on orientation. Pinned through P16's own output:
    // the boundary of ONE tetrahedron is its four faces, each wound by P16's table.
    meshing::MeshBuilder builder;
    std::array<NodeId, 4> nodes{};
    const std::array<Point3D, 4> corners{Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm},
                                         Point3D{0_mm, 10_mm, 0_mm}, Point3D{0_mm, 0_mm, 10_mm}};
    for (std::size_t i = 0; i < 4; ++i) {
        nodes[i] = require(builder.addNode(corners[i]));
    }
    REQUIRE(builder.addTetrahedron(nodes, kRegion));
    const std::vector<std::array<NodeId, 3>> p16 = meshing::tetrahedralBoundary(builder.build());
    REQUIRE(p16.size() == 4);
    for (const auto& local : cfd::kTetFaces) {
        const std::array<NodeId, 3> face{nodes[local[0]], nodes[local[1]], nodes[local[2]]};
        CHECK(std::ranges::count(p16, face) == 1);
    }
}

TEST_CASE("CfdMesh_AnUnnamedPatchIsCountedNotRefused", "[cfd][mesh]") {
    // The drilled wall is mapped (the attribution is complete) but carries no
    // name. The mesh satisfies the contract; the patch is COUNTED, because no
    // GeometryReference can reach it and P19-BC-001 must refuse an analysis that
    // leaves a facet without a condition (ADR-058). Not refused here: it is a
    // fact about naming, not about the mesh.
    MeshedFluid fluid(boredDocument());
    const Result<cfd::CfdModel> model = fluid.prepare();
    INFO((model.has_value() ? std::string{} : model.error().message));
    REQUIRE(model.has_value());
    REQUIRE(model->map().report().complete());
    REQUIRE(model->map().report().unnamedFaceCount == 1);

    const Result<CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    INFO((contract.has_value() ? std::string{} : contract.error().message));
    REQUIRE(contract.has_value());
    CHECK(contract->patches() == 7);
    CHECK(contract->patches() == model->map().faces().size());
    CHECK(contract->unnamedPatches() == 1);
    std::size_t unnamed = 0;
    for (const meshing::MappedFace& face : model->map().faces()) {
        unnamed += face.names.empty() ? 1U : 0U;
    }
    CHECK(contract->unnamedPatches() == unnamed);
    CHECK(contract->census().components == 1);
}

TEST_CASE("CfdMesh_AHandBuiltTwoCellMeshSatisfiesTheContract", "[cfd][mesh]") {
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const CfdMeshInspection inspection = inspect(mesh);
    INFO((inspection.findings.empty() ? std::string{} : inspection.findings.front().message));
    REQUIRE(inspection.satisfiesContract());
    CHECK(inspection.census == cfd::CfdMeshCensus{.cells = 2, .interiorFaces = 1, .boundaryFaces = 6,
                                                  .nonManifoldFaces = 0, .boundaryTriangles = 6, .components = 1});
}

// ---------------------------------------------------------------------------
// Every refusal, on a mesh built to fail it
// ---------------------------------------------------------------------------

TEST_CASE("CfdMesh_RefusesAMeshWithNoCells", "[cfd][mesh]") {
    meshing::MeshBuilder builder;
    (void)require(builder.addNode(Point3D{}));
    const CfdMeshInspection inspection = cfd::inspectCfdMesh(builder.build(), completeMapping(0));
    REQUIRE_FALSE(inspection.satisfiesContract());
    CHECK(inspection.findings.front().problem == CfdMeshProblem::EmptyMesh);
}

TEST_CASE("CfdMesh_RefusesAZeroOrNegativeCellVolume", "[cfd][mesh]") {
    SECTION("an inverted cell (two vertices swapped)") {
        const std::vector<std::array<std::size_t, 4>> inverted{{1, 0, 2, 3}};
        const CfdMeshInspection inspection = inspect(buildMesh(twoCellPoints(), inverted));
        REQUIRE_FALSE(inspection.satisfiesContract());
        CHECK(inspection.findings.front().problem == CfdMeshProblem::NonPositiveCellVolume);
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("signed volume of -"));
        // Its four faces, wound by the table, point INTO it: each is counted.
        REQUIRE(problemsOf(inspection) ==
                std::vector{CfdMeshProblem::NonPositiveCellVolume, CfdMeshProblem::InwardBoundaryFace});
        CHECK(inspection.findings[1].count == 4);
    }
    SECTION("a flat cell (four coplanar vertices)") {
        std::vector<Point3D> points = twoCellPoints();
        points[3] = Point3D{5_mm, 5_mm, 0_mm};
        const std::vector<std::array<std::size_t, 4>> flat{{0, 1, 2, 3}};
        const CfdMeshInspection inspection = inspect(buildMesh(points, flat));
        REQUIRE_FALSE(inspection.satisfiesContract());
        CHECK(inspection.findings.front().problem == CfdMeshProblem::NonPositiveCellVolume);
        // Its faces have no outward side at all, and are said to be degenerate
        // rather than "wound into" anything.
        REQUIRE(problemsOf(inspection) ==
                std::vector{CfdMeshProblem::NonPositiveCellVolume, CfdMeshProblem::InwardBoundaryFace});
        CHECK(inspection.findings[1].count == 4);
        CHECK_THAT(inspection.findings[1].message, ContainsSubstring("is degenerate against cell"));
    }
    SECTION("a cell whose volume is not a finite number") {
        // MeshBuilder refuses a non-finite coordinate, but finite coordinates of
        // 1e120 m give a volume of order 1e360 m^3, which overflows to +infinity:
        // positive, and still unusable.
        const Length big = Length::fromSi(1e120);
        const Length zero = Length::fromSi(0.0);
        const std::vector<Point3D> points{Point3D{zero, zero, zero}, Point3D{big, zero, zero},
                                          Point3D{zero, big, zero}, Point3D{zero, zero, big}};
        const std::vector<std::array<std::size_t, 4>> huge{{0, 1, 2, 3}};
        const CfdMeshInspection inspection = inspect(buildMesh(points, huge));
        REQUIRE_FALSE(inspection.satisfiesContract());
        CHECK(inspection.findings.front().problem == CfdMeshProblem::NonPositiveCellVolume);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("signed volume of inf"));
    }
}

TEST_CASE("CfdMesh_RefusesAFaceSharedByThreeCells", "[cfd][mesh]") {
    // A third cell on the far side of A and B's shared face: three cells claim it.
    std::vector<std::array<std::size_t, 4>> three = kTwoCells;
    three.push_back({1, 2, 3, 5});
    SECTION("the face alone") {
        const CfdMeshInspection inspection = inspect(buildMesh(twoCellPoints(), three));
        REQUIRE_FALSE(inspection.satisfiesContract());
        CHECK(inspection.census.nonManifoldFaces == 1);
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::NonManifoldFace});
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("shared by 3 cells"));
    }
    SECTION("and a boundary triangle laid on it") {
        // A triangle on a three-way face has no single owner: an ownership
        // refusal too, naming the three.
        const CfdMeshInspection inspection =
            inspect(buildMesh(twoCellPoints(), three, Spoil{.coverAnInteriorFace = true}));
        CHECK(problemsOf(inspection) ==
              std::vector{CfdMeshProblem::NonManifoldFace, CfdMeshProblem::InvalidFaceOwnership});
        CHECK_THAT(inspection.findings[1].message, ContainsSubstring("lies on a face shared by 3 cells"));
    }
}

TEST_CASE("CfdMesh_RefusesTwoCellsFoldedOntoOneSideOfTheirFace", "[cfd][mesh]") {
    // B = (1,2,3,4) and C = (1,2,3,5): both positive, both on the SAME side of the
    // face (1,2,3), sharing it and folded over one another. Everything else about
    // the pair is in order -- face-connected, closed, every boundary triangle
    // outward -- so only the orientation of the shared face shows it. Without
    // this refusal "S points from owner to neighbour" would be false here.
    const std::vector<std::array<std::size_t, 4>> folded{{1, 2, 3, 4}, {1, 2, 3, 5}};
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), folded);
    const CfdMeshInspection inspection = inspect(mesh);
    CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::InconsistentFaceOrientation});
    CHECK(inspection.findings.front().count == 1);
    CHECK_THAT(inspection.findings.front().message,
               ContainsSubstring(std::format("cells {} and {} traverse their shared face",
                                             mesh.tetrahedra()[0].id.value(), mesh.tetrahedra()[1].id.value())));
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("in the same direction"));
    CHECK(inspection.census.interiorFaces == 1);
    CHECK(inspection.census.components == 1);

    // The same two cells listed in other corner orders (still positive) are the
    // same fold: the check is on the face's traversal, not on how a cell lists it.
    const std::vector<std::array<std::size_t, 4>> rotated{{2, 3, 1, 4}, {3, 1, 2, 5}};
    CHECK(problemsOf(inspect(buildMesh(twoCellPoints(), rotated))) ==
          std::vector{CfdMeshProblem::InconsistentFaceOrientation});
    // And a genuine neighbour listed in a rotated corner order is not a fold.
    const std::vector<std::array<std::size_t, 4>> neighbours{{0, 1, 2, 3}, {2, 3, 1, 4}};
    CHECK(inspect(buildMesh(twoCellPoints(), neighbours)).satisfiesContract());
}

TEST_CASE("CfdMesh_RefusesCellsThatAreNotOneFaceConnectedSet", "[cfd][mesh]") {
    // Cell A, and cell B = A moved up by 10 mm: B's base corner is A's apex, so the
    // two touch at ONE vertex and share no face. Then the same B moved further, so
    // they touch nowhere. Then a pinch along an EDGE. Each is two flows with one
    // pressure datum between them.
    const auto twoPieces = [](const Point3D& offset, std::size_t shared) {
        std::vector<Point3D> points{Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm},
                                    Point3D{0_mm, 10_mm, 0_mm}, Point3D{0_mm, 0_mm, 10_mm}};
        std::vector<std::array<std::size_t, 4>> cells{{0, 1, 2, 3}};
        std::array<std::size_t, 4> b{};
        for (std::size_t i = 0; i < 4; ++i) {
            if (i < shared) {
                b[i] = 3; // B's first corner is A's apex
                continue;
            }
            const Point3D& p = points[i];
            points.push_back(Point3D{p.x + offset.x, p.y + offset.y, p.z + offset.z});
            b[i] = points.size() - 1;
        }
        cells.push_back(b);
        return std::pair{points, cells};
    };

    SECTION("touching at one vertex") {
        const auto [points, cells] = twoPieces(Point3D{0_mm, 0_mm, 10_mm}, 1);
        const meshing::Mesh mesh = buildMesh(points, cells);
        const CfdMeshInspection inspection = inspect(mesh);
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::DisconnectedCells});
        CHECK(inspection.census.components == 2);
        CHECK(inspection.census.interiorFaces == 0);
        // The message names the first cell outside the FIRST cell's piece, and the
        // first cell: both by their P16 handles.
        CHECK_THAT(inspection.findings.front().message,
                   ContainsSubstring(std::format("2 face-connected pieces; cell {} shares no chain of faces with cell {}",
                                                 mesh.tetrahedra()[1].id.value(), mesh.tetrahedra()[0].id.value())));
    }
    SECTION("three cells, the third apart: the stray named is the first one outside") {
        // A and B share a face; C is A moved far away. Pieces {A, B} and {C}.
        const std::vector<Point3D> base = twoCellPoints();
        std::vector<Point3D> points = base;
        for (std::size_t i = 0; i < 4; ++i) {
            points.push_back(Point3D{base[i].x + 100_mm, base[i].y, base[i].z});
        }
        std::vector<std::array<std::size_t, 4>> cells = kTwoCells;
        cells.push_back({6, 7, 8, 9});
        const meshing::Mesh mesh = buildMesh(points, cells);
        const CfdMeshInspection inspection = inspect(mesh);
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::DisconnectedCells});
        CHECK(inspection.census.components == 2);
        CHECK_THAT(inspection.findings.front().message,
                   ContainsSubstring(std::format("cell {} shares no chain of faces with cell {}",
                                                 mesh.tetrahedra()[2].id.value(), mesh.tetrahedra()[0].id.value())));
    }
    SECTION("touching nowhere") {
        const auto [points, cells] = twoPieces(Point3D{50_mm, 50_mm, 50_mm}, 0);
        const CfdMeshInspection inspection = inspect(buildMesh(points, cells));
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::DisconnectedCells});
        CHECK(inspection.census.components == 2);
        CHECK(inspection.findings.front().count == 1);
    }
    SECTION("three pieces: two beyond the first") {
        std::vector<Point3D> points;
        std::vector<std::array<std::size_t, 4>> cells;
        for (std::size_t piece = 0; piece < 3; ++piece) {
            const Length shift = Length::fromSi(0.05 * static_cast<double>(piece));
            for (const Point3D& p : {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
                                     Point3D{0_mm, 0_mm, 10_mm}}) {
                points.push_back(Point3D{p.x + shift, p.y, p.z});
            }
            const std::size_t at = 4 * piece;
            cells.push_back({at, at + 1, at + 2, at + 3});
        }
        const CfdMeshInspection inspection = inspect(buildMesh(points, cells));
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::DisconnectedCells});
        CHECK(inspection.census.components == 3);
        CHECK(inspection.findings.front().count == 2);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("3 face-connected pieces"));
    }
    SECTION("a pinch along an edge") {
        // A = (0,1,2,3); B shares A's edge 2-3 and lies on the far side of the
        // plane x = 0 from A, so it shares no face: B = (2, 3, (-10,0,0), (-10,10,10)).
        std::vector<Point3D> points{Point3D{0_mm, 0_mm, 0_mm},    Point3D{10_mm, 0_mm, 0_mm},
                                    Point3D{0_mm, 10_mm, 0_mm},   Point3D{0_mm, 0_mm, 10_mm},
                                    Point3D{-10_mm, 0_mm, 0_mm},  Point3D{-10_mm, 10_mm, 10_mm}};
        std::vector<std::array<std::size_t, 4>> cells{{0, 1, 2, 3}, {2, 3, 4, 5}};
        // Wind B positively whichever way the corners fell.
        if (meshing::signedVolume(points[2], points[3], points[4], points[5]).si() < 0.0) {
            cells[1] = {3, 2, 4, 5};
        }
        const CfdMeshInspection inspection = inspect(buildMesh(points, cells));
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::DisconnectedCells});
        CHECK(inspection.census.components == 2);
    }
    SECTION("but an edge shared only through a face-connected fan is ONE piece, and accepted") {
        // Three cells around the edge v0-v1 (the z axis): A = (v0,v1,a,b),
        // C = (v0,v1,b,c), B = (v0,v1,c,d). A and B share ONLY the edge; C joins
        // them by faces. Nothing a flux crosses is missing, so it is not refused
        // (ADR-063 §5: the finite-volume definition of manifold is face-based).
        const std::vector<Point3D> points{Point3D{0_mm, 0_mm, 0_mm},  Point3D{0_mm, 0_mm, 10_mm},
                                          Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
                                          Point3D{-10_mm, 0_mm, 0_mm}, Point3D{0_mm, -10_mm, 0_mm}};
        std::vector<std::array<std::size_t, 4>> cells{{0, 1, 2, 3}, {0, 1, 3, 4}, {0, 1, 4, 5}};
        for (auto& cell : cells) {
            if (meshing::signedVolume(points[cell[0]], points[cell[1]], points[cell[2]], points[cell[3]]).si() < 0.0) {
                std::swap(cell[2], cell[3]);
            }
        }
        const CfdMeshInspection inspection = inspect(buildMesh(points, cells));
        INFO((inspection.findings.empty() ? std::string{} : inspection.findings.front().message));
        CHECK(inspection.satisfiesContract());
        CHECK(inspection.census.components == 1);
        CHECK(inspection.census.interiorFaces == 2);
    }
    SECTION("a mesh with no cells has no piece, and is refused as empty, not as disconnected") {
        meshing::MeshBuilder builder;
        (void)require(builder.addNode(Point3D{}));
        const CfdMeshInspection inspection = cfd::inspectCfdMesh(builder.build(), completeMapping(0));
        CHECK(inspection.census.components == 0);
        // Empty, and (P16's own `complete()` needs at least one facet) unattributed;
        // never disconnected.
        CHECK(problemsOf(inspection) ==
              std::vector{CfdMeshProblem::EmptyMesh, CfdMeshProblem::UnattributedBoundary});
    }
}

TEST_CASE("CfdMesh_RefusesInvalidBoundaryFaceOwnership", "[cfd][mesh]") {
    SECTION("a boundary triangle on an interior face") {
        const CfdMeshInspection inspection =
            inspect(buildMesh(twoCellPoints(), kTwoCells, Spoil{.coverAnInteriorFace = true}));
        // The spoiled triangle is wound INTO the cell: had the gate gone on to the
        // orientation check it would also report an inward face.
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::InvalidFaceOwnership});
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("shared by 2 cells"));
    }
    SECTION("two boundary triangles on one face") {
        // The second is the first reversed, so it would be "wound into" the cell
        // if the duplicate were not refused first.
        const CfdMeshInspection inspection =
            inspect(buildMesh(twoCellPoints(), kTwoCells, Spoil{.duplicateFirst = true}));
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::InvalidFaceOwnership});
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("claim the same face"));
    }
    SECTION("a boundary triangle that is a face of no cell") {
        const CfdMeshInspection inspection =
            inspect(buildMesh(twoCellPoints(), kTwoCells, Spoil{.addAStrayTriangle = true}));
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::InvalidFaceOwnership});
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("is a face of no cell"));
    }
}

TEST_CASE("CfdMesh_RefusesABoundaryFaceWoundIntoItsCell", "[cfd][mesh]") {
    const CfdMeshInspection inspection = inspect(buildMesh(twoCellPoints(), kTwoCells, Spoil{.reverseFirst = true}));
    CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::InwardBoundaryFace});
    CHECK(inspection.findings.front().count == 1);
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("is wound into cell"));
}

TEST_CASE("CfdMesh_RefusesAnUnclosedDomain", "[cfd][mesh]") {
    const CfdMeshInspection inspection = inspect(buildMesh(twoCellPoints(), kTwoCells, Spoil{.omitFirst = true}));
    CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::UnclosedDomain});
    CHECK(inspection.census.boundaryFaces == 6);
    CHECK(inspection.census.boundaryTriangles == 5);
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("is on no boundary triangle"));
}

TEST_CASE("CfdMesh_RefusesAnUnattributedBoundaryAndNeverPicksTheNearestFace", "[cfd][mesh]") {
    // P16 attributes boundary facets to CAD faces by exact position and REPORTS a
    // facet it could not attribute. The gate refuses the report rather than
    // assigning the facet to the nearest face: no nearest-face fallback exists.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    SECTION("a facet attributed to no CAD face") {
        meshing::GeometryMeshMappingReport report = completeMapping(6);
        report.mappedFacetCount = 5;
        report.unmappedFacetCount = 1;
        const CfdMeshInspection inspection = cfd::inspectCfdMesh(mesh, report);
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::UnattributedBoundary});
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("1 boundary facet(s) attributed to no CAD face"));
    }
    SECTION("a CAD face given no facet, and a facet given to several faces") {
        meshing::GeometryMeshMappingReport report = completeMapping(6);
        report.facesWithoutFacets = 1;
        report.facetsWithSeveralFaces = 2;
        const CfdMeshInspection inspection = cfd::inspectCfdMesh(mesh, report);
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::UnattributedBoundary});
        CHECK(inspection.findings.front().count == 3);
    }
    SECTION("a complete report that counts a different number of facets") {
        const CfdMeshInspection inspection = cfd::inspectCfdMesh(mesh, completeMapping(7));
        CHECK(problemsOf(inspection) == std::vector{CfdMeshProblem::UnattributedBoundary});
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message,
                   ContainsSubstring("the map attributes 7 boundary facet(s) but the mesh holds 6"));
    }
}

TEST_CASE("CfdMesh_ReportsEveryProblemInTheDocumentedOrder", "[cfd][mesh]") {
    // Seven defects at once, every one reported, in the enum's order -- not the
    // first one found. A, B, C share the face (1,2,3) three ways; D is an INVERTED
    // copy of A far away (a bad volume, and a second piece); a stray triangle, a
    // reversed one and an uncovered face; and no attribution at all. (A fold
    // needs a face only two cells share, and an empty mesh nothing else: each is
    // shown alone.)
    std::vector<Point3D> points = twoCellPoints();
    const std::vector<Point3D> base = twoCellPoints();
    for (std::size_t i = 0; i < 4; ++i) {
        points.push_back(Point3D{base[i].x + 100_mm, base[i].y, base[i].z});
    }
    std::vector<std::array<std::size_t, 4>> cells = kTwoCells;
    cells.push_back({1, 2, 3, 5});
    cells.push_back({7, 6, 8, 9});
    const CfdMeshInspection inspection = cfd::inspectCfdMesh(
        buildMesh(points, cells, Spoil{.omitLast = true, .reverseFirst = true, .addAStrayTriangle = true}),
        meshing::GeometryMeshMappingReport{});
    CHECK(problemsOf(inspection) ==
          std::vector{CfdMeshProblem::NonPositiveCellVolume, CfdMeshProblem::NonManifoldFace,
                      CfdMeshProblem::DisconnectedCells, CfdMeshProblem::InvalidFaceOwnership,
                      CfdMeshProblem::InwardBoundaryFace, CfdMeshProblem::UnclosedDomain,
                      CfdMeshProblem::UnattributedBoundary});
    for (const cfd::CfdMeshFinding& finding : inspection.findings) {
        CHECK(finding.count >= 1);
        CHECK(finding.message.starts_with(cfd::toString(finding.problem)));
    }
    CHECK(inspection.firstProblem() == CfdMeshProblem::NonPositiveCellVolume);
}

TEST_CASE("CfdMesh_TheRefusalNamesTheControlAndEveryFinding", "[cfd][mesh]") {
    // What requireCfdMesh returns when the inspection fails. A CfdModel always
    // holds a P16-validated mesh, so the refusal is shown on a hand-built one
    // through the same function the gate calls.
    const CfdMeshInspection inspection =
        cfd::inspectCfdMesh(buildMesh(twoCellPoints(), kTwoCells, Spoil{.omitFirst = true, .addAStrayTriangle = true}),
                            meshing::GeometryMeshMappingReport{});
    REQUIRE(inspection.findings.size() == 3);
    const Error error = cfd::cfdMeshRefusal(inspection, MeshControlId::fromValue(42));
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK(error.message ==
          std::format("the mesh of meshing control 42 does not satisfy the CFD mesh contract: {}; {}; {}",
                      inspection.findings[0].message, inspection.findings[1].message,
                      inspection.findings[2].message));
    CHECK(inspection.firstProblem() == CfdMeshProblem::InvalidFaceOwnership);
    CHECK_FALSE(inspect(buildMesh(twoCellPoints(), kTwoCells)).firstProblem().has_value());
}

// ---------------------------------------------------------------------------
// The gate on a real input
// ---------------------------------------------------------------------------

TEST_CASE("CfdMesh_TheContractBindsItsMeshByStampNeverByCounts", "[cfd][mesh]") {
    // A contract proved for one generation says nothing about the next, however
    // equal their census: a remesh of the same body gives identical counts and
    // identical NodeIds, and a contract that compared counts would call it proved.
    MeshedFluid fluid(MeshReferenceModelKind::Block);
    const Result<cfd::CfdModel> first = fluid.prepare();
    REQUIRE(first.has_value());
    const Result<CfdMeshContract> contract = cfd::requireCfdMesh(*first);
    REQUIRE(contract.has_value());

    (void)fluid.model.require(); // the same body, the same settings, a new generation
    const Result<cfd::CfdModel> second = fluid.prepare();
    REQUIRE(second.has_value());
    const Result<CfdMeshContract> again = cfd::requireCfdMesh(*second);
    REQUIRE(again.has_value());

    CHECK(again->census() == contract->census());
    CHECK_FALSE(contract->describes(second->mesh().mesh()));
    CHECK(again->describes(second->mesh().mesh()));
    CHECK_FALSE(*again == *contract);
}

TEST_CASE("CfdMesh_TheGateIsStagedAfterTheInputBoundaryAndItsProofCannotBeForged", "[cfd][mesh][compile]") {
    // It takes a CfdModel: the mesh it inspects is already current, eligible and
    // paired with its map (ADR-060). And its result cannot be made by a caller.
    static_assert(std::is_same_v<decltype(&cfd::requireCfdMesh), Result<CfdMeshContract> (*)(const cfd::CfdModel&)>);
    static_assert(!std::is_default_constructible_v<CfdMeshContract>);
    static_assert(!std::is_constructible_v<CfdMeshContract, meshing::MeshStamp, meshing::GeometryRevision,
                                           cfd::CfdMeshCensus, std::size_t, std::size_t>);
    // A value: it borrows nothing, so it cannot dangle when the mesher replaces
    // the mesh -- it simply stops describing it.
    static_assert(std::is_copy_constructible_v<CfdMeshContract>);
    SUCCEED("the proof that a mesh met the contract cannot be made without checking it");
}

// ---------------------------------------------------------------------------
// Determinism and names
// ---------------------------------------------------------------------------

TEST_CASE("CfdMesh_TheInspectionIsDeterministic", "[cfd][mesh][determinism]") {
    // The same mesh inspected twice, and the same mesh BUILT twice (two stamps),
    // give the same census and the same findings, message for message.
    const meshing::Mesh a = buildMesh(twoCellPoints(), kTwoCells, Spoil{.reverseFirst = true});
    const meshing::Mesh b = buildMesh(twoCellPoints(), kTwoCells, Spoil{.reverseFirst = true});
    REQUIRE_FALSE(a.stamp() == b.stamp());
    CHECK(inspect(a) == inspect(a));
    CHECK(inspect(a) == inspect(b));

    MeshedFluid fluid(MeshReferenceModelKind::Cylinder);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const auto& mesh = model->mesh().mesh();
    const auto& report = model->map().report();
    CHECK(cfd::inspectCfdMesh(mesh, report) == cfd::inspectCfdMesh(mesh, report));
}

TEST_CASE("CfdMesh_EveryProblemHasADistinctName", "[cfd][mesh][names]") {
    std::vector<std::string_view> names;
    for (const CfdMeshProblem problem : cfd::kCfdMeshProblems) {
        CHECK(cfd::toString(problem) != "unknown");
        names.push_back(cfd::toString(problem));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    for (std::size_t i = 0; i < cfd::kCfdMeshProblems.size(); ++i) {
        CHECK(cfd::kCfdMeshProblems[i] == static_cast<CfdMeshProblem>(i));
    }
    CHECK(cfd::toString(static_cast<CfdMeshProblem>(cfd::kCfdMeshProblems.size())) == "unknown");
    // The local face table and its opposite vertices: each face omits exactly its
    // opposite vertex and contains the other three, and the four opposites are the
    // four vertices once each.
    std::array<bool, 4> seen{};
    for (std::size_t k = 0; k < cfd::kTetFaces.size(); ++k) {
        const std::size_t opposite = cfd::kTetFaceOpposite[k];
        CHECK(std::ranges::find(cfd::kTetFaces[k], opposite) == cfd::kTetFaces[k].end());
        for (std::size_t v = 0; v < 4; ++v) {
            if (v != opposite) {
                CHECK(std::ranges::find(cfd::kTetFaces[k], v) != cfd::kTetFaces[k].end());
            }
        }
        seen[opposite] = true;
    }
    CHECK(seen == std::array{true, true, true, true});

    // And each face is wound OUTWARD on the unit tetrahedron (0, x, y, z), whose
    // volume is +1/6: the face normal points away from the opposite vertex.
    const std::array<std::array<double, 3>, 4> unit{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    for (std::size_t k = 0; k < cfd::kTetFaces.size(); ++k) {
        const auto& p = unit[cfd::kTetFaces[k][0]];
        const auto& q = unit[cfd::kTetFaces[k][1]];
        const auto& r = unit[cfd::kTetFaces[k][2]];
        const auto& apex = unit[cfd::kTetFaceOpposite[k]];
        const std::array<double, 3> u{q[0] - p[0], q[1] - p[1], q[2] - p[2]};
        const std::array<double, 3> w{r[0] - p[0], r[1] - p[1], r[2] - p[2]};
        const std::array<double, 3> n{u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
        const double toward = n[0] * (apex[0] - p[0]) + n[1] * (apex[1] - p[1]) + n[2] * (apex[2] - p[2]);
        CHECK(toward < 0.0);
    }
}
