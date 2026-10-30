// P19-TOPO-001: the finite-volume topology derived from a proved P16 mesh
// (ADR-064).
//
// TWO KINDS OF EVIDENCE, as P19-MESH-001 used. Real P16 meshes -- every reference
// model P16 qualified -- must yield a topology whose census agrees with the mesh
// contract's census AND with an INDEPENDENT face incidence written for P16's own
// qualification (tests/reference/MeshTestSupport.hpp's `faceIncidence`). And every
// refusal must be shown on a mesh built by hand to fail it, because a mesh that
// holds a CfdMeshContract cannot fail any of them -- which is the point of
// `buildCfdTopology` taking a plain Mesh and a plain patch list.
//
// WHAT IS CHECKED INDEPENDENTLY RATHER THAN AGAINST THE BUILDER. The winding of
// every face is checked geometrically, against the owner's opposite vertex, not by
// comparing with the table the builder used; the owner rule is checked as the
// ORDER relation ADR-063 states (owner < neighbour) rather than by re-deriving
// "first claimant"; and the cell/face adjacency is checked for mutual consistency
// in both directions, so a face that named the wrong cells would have to be wrong
// in both tables in the same way to pass.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using bettercad::reference::MeshReferenceModelKind;
using Catch::Matchers::ContainsSubstring;
using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdTopology;
using cfd::CfdTopologyProblem;
using cfd::FaceIndex;
using cfd::PatchIndex;
using meshing::ElementId;
using meshing::NodeId;

namespace {

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// ADR-063's face table, written out again HERE rather than included from the
/// header, so a test fixture that agreed with a corrupted table would not agree
/// with this one.
constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

/// How a hand-built mesh is to be spoiled, one way per refusal.
struct Spoil {
    bool omitOneTriangle = false;   // a one-cell face with no triangle   -> MissingNeighbour
    bool strayTriangle = false;     // a triangle that is no cell's face  -> UnownedBoundaryFace
    bool coverInteriorFace = false; // a triangle on a shared face        -> BoundaryFaceWithTwoOwners
    bool duplicateTriangle = false; // two triangles on one face          -> DuplicateBoundaryFace
};

/// Builds a mesh from @p points and @p tets and covers every face exactly one tet
/// uses with a boundary triangle wound outward of that tet, then spoils it.
meshing::Mesh buildMesh(const std::vector<Point3D>& points, const std::vector<std::array<std::size_t, 4>>& tets,
                        Spoil spoil = {}) {
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
    bool firstBoundary = true;
    for (const auto& [key, use] : uses) {
        const auto& [count, w] = use;
        if (count >= 2) {
            if (spoil.coverInteriorFace) {
                REQUIRE(builder.addTriangle({nodes[w[0]], nodes[w[1]], nodes[w[2]]}, kRegion));
                spoil.coverInteriorFace = false;
            }
            continue;
        }
        if (firstBoundary && spoil.omitOneTriangle) {
            firstBoundary = false;
            continue;
        }
        REQUIRE(builder.addTriangle({nodes[w[0]], nodes[w[1]], nodes[w[2]]}, kRegion));
        if (firstBoundary && spoil.duplicateTriangle) {
            REQUIRE(builder.addTriangle({nodes[w[0]], nodes[w[1]], nodes[w[2]]}, kRegion));
        }
        firstBoundary = false;
    }
    if (spoil.strayTriangle) {
        const NodeId stray = require(builder.addNode(Point3D{5_mm, 5_mm, 5_mm}));
        REQUIRE(builder.addTriangle({nodes[0], nodes[1], stray}, kRegion));
    }
    return builder.build();
}

/// Two cells sharing one face: A = (0,1,2,3), B = (1,2,3,4), both positive.
/// 7 faces: 1 interior, 6 boundary, and 4 * 2 == 2 * 1 + 6.
std::vector<Point3D> twoCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm},    Point3D{10_mm, 0_mm, 0_mm},   Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm},   Point3D{10_mm, 10_mm, 10_mm}, Point3D{20_mm, 20_mm, 20_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};
/// Three cells all claiming the face (1,2,3): non-manifold.
const std::vector<std::array<std::size_t, 4>> kThreeOnAFace{{0, 1, 2, 3}, {1, 2, 3, 4}, {1, 2, 3, 5}};

/// A single INVERTED cell: {0,1,3,2} has a negative signed volume, so kTetFaces
/// winds its faces INTO it. The configuration whose silent acceptance an
/// adversarial review found.
const std::vector<std::array<std::size_t, 4>> kInvertedCell{{0, 1, 3, 2}};

/// Two POSITIVELY oriented cells FOLDED onto one side of their shared face.
/// Both apexes lie on the same side of the plane through nodes 1, 2 and 3, so
/// both wind that face the same way and neither volume is negative -- which is
/// why a volume check alone does not catch it.
std::vector<Point3D> foldedPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}, Point3D{1_mm, 1_mm, 1_mm}};
}
const std::vector<std::array<std::size_t, 4>> kFoldedCells{{0, 1, 2, 3}, {4, 1, 2, 3}};

/// One patch holding every boundary triangle of @p mesh, which is what a
/// single-CAD-face body's map reports.
std::vector<meshing::MappedFace> onePatch(const meshing::Mesh& mesh) {
    meshing::MappedFace face;
    face.index = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        face.facets.push_back(triangle.id);
    }
    return {face};
}

/// @p groups patches, the triangles dealt out round-robin, so patch membership is
/// exercised with more than one patch and with a known expected partition.
std::vector<meshing::MappedFace> patchesOf(const meshing::Mesh& mesh, std::size_t groups) {
    std::vector<meshing::MappedFace> patches(groups);
    for (std::size_t p = 0; p < groups; ++p) {
        patches[p].index = 100 + p; // a CAD face position, deliberately not p
    }
    std::size_t next = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        patches[next++ % groups].facets.push_back(triangle.id);
    }
    return patches;
}

std::vector<CfdTopologyProblem> problemsOf(const cfd::CfdTopologyInspection& inspection) {
    std::vector<CfdTopologyProblem> problems;
    for (const cfd::CfdTopologyFinding& finding : inspection.findings) {
        problems.push_back(finding.problem);
    }
    return problems;
}

using Vec = std::array<double, 3>;
Vec si(const Point3D& p) { return {p.x.si(), p.y.si(), p.z.si()}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dotp(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec crossp(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

/// The outward normal of @p face's three nodes, by the right-hand rule.
Vec normalOf(const meshing::Mesh& mesh, const std::array<NodeId, 3>& face) {
    const Vec a = si(mesh.findNode(face[0])->position);
    const Vec b = si(mesh.findNode(face[1])->position);
    const Vec c = si(mesh.findNode(face[2])->position);
    return crossp(sub(b, a), sub(c, a));
}

/// The vertex of tetrahedron @p element that is not one of @p face's three nodes.
NodeId apexOf(const meshing::Mesh& mesh, ElementId element, const std::array<NodeId, 3>& face) {
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        if (tet.id != element) {
            continue;
        }
        for (const NodeId node : tet.nodes) {
            if (std::ranges::find(face, node) == face.end()) {
                return node;
            }
        }
    }
    return NodeId{};
}

/// Whether @p other traverses the same three nodes in the SAME cyclic direction
/// as @p wound. Comparing the two as sorted sets would accept a reversed face.
bool isRotation(const std::array<NodeId, 3>& wound, const std::array<NodeId, 3>& other) {
    for (std::size_t shift = 0; shift < 3; ++shift) {
        if (other[0] == wound[shift] && other[1] == wound[(shift + 1) % 3] &&
            other[2] == wound[(shift + 2) % 3]) {
            return true;
        }
    }
    return false;
}

/// EVERY STRUCTURAL INVARIANT THE MILESTONE CLAIMS, on one topology.
///
/// Factored out and called from the all-models sweep for a reason the first draft
/// of this file got wrong: each property had been checked on the ONE model chosen
/// for it -- the owner rule on the block, the winding on the cylinder, adjacency
/// on the thin plate -- so RM-MESH-06, the transformed body, was never structurally
/// tested at all, and it is the one model where a placement transform could invert
/// a winding or flip the owner relation. A review found that; this is the fix.
void checkStructure(const CfdTopology& topology, const meshing::Mesh& mesh) {
    std::map<ElementId, std::array<NodeId, 4>> tets;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        tets.emplace(tet.id, tet.nodes);
    }

    // THE SORTED ORDER cellIndexOf's binary search depends on. The header claims
    // this is asserted rather than trusted, and now it is: ascending ElementId,
    // which is also what makes "lower CellIndex" and ADR-063's "first claimant in
    // ascending ElementId order" the same rule.
    for (std::size_t c = 1; c < topology.cells(); ++c) {
        CHECK(topology.cellOf(CellIndex::fromValue(c - 1)) < topology.cellOf(CellIndex::fromValue(c)));
    }

    std::vector<std::size_t> appearances(topology.faces(), 0);
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = topology.owner(face);
        REQUIRE(owner.isValid());
        REQUIRE(owner.value() < topology.cells());
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        const std::array<NodeId, 3> nodes = topology.faceNodes(face);

        // The face really is a face of its owner: its three nodes are three of the
        // owner tetrahedron's four. apexOf alone does not establish this.
        const auto ownerTet = tets.find(topology.cellOf(owner));
        REQUIRE(ownerTet != tets.end());
        for (const NodeId node : nodes) {
            CHECK(std::ranges::find(ownerTet->second, node) != ownerTet->second.end());
        }

        // And it is wound OUTWARD of that owner, checked geometrically against the
        // owner's opposite vertex rather than against the builder's own table.
        const NodeId apex = apexOf(mesh, topology.cellOf(owner), nodes);
        REQUIRE(apex.isValid());
        const Vec toApex = sub(si(mesh.findNode(apex)->position), si(mesh.findNode(nodes[0])->position));
        CHECK(dotp(normalOf(mesh, nodes), toApex) < 0.0);

        if (topology.isInternal(face)) {
            REQUIRE(neighbour.has_value());
            CHECK(owner < *neighbour);
            CHECK(topology.cellOf(owner) < topology.cellOf(*neighbour));
            const auto neighbourTet = tets.find(topology.cellOf(*neighbour));
            REQUIRE(neighbourTet != tets.end());
            for (const NodeId node : nodes) {
                CHECK(std::ranges::find(neighbourTet->second, node) != neighbourTet->second.end());
            }
        } else {
            CHECK(topology.isBoundary(face));
            CHECK_FALSE(neighbour.has_value());
            const std::optional<BoundaryFaceIndex> boundary = topology.boundaryFaceOf(face);
            REQUIRE(boundary.has_value());
            const ElementId triangle = topology.triangleOf(*boundary);
            const auto found = std::ranges::find_if(
                mesh.triangles(), [triangle](const meshing::Triangle& t) { return t.id == triangle; });
            REQUIRE(found != mesh.triangles().end());
            // A CYCLIC ROTATION, not the same set: a boundary face wound opposite
            // to the P16 triangle it lies on would pass a set comparison, and its
            // area vector would point into the fluid.
            CHECK(isRotation(nodes, found->nodes));
        }
    }

    // Both adjacency directions, cross-checked: an internal face is listed by
    // exactly its two cells, a boundary face by its one.
    for (std::size_t c = 0; c < topology.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::span<const FaceIndex> faces = topology.cellFaces(cell);
        REQUIRE(faces.size() == 4);
        CHECK(std::ranges::is_sorted(faces));
        CHECK(std::ranges::adjacent_find(faces) == faces.end());
        for (const FaceIndex face : faces) {
            REQUIRE(face.value() < topology.faces());
            ++appearances[face.value()];
            const CellIndex owner = topology.owner(face);
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            CHECK((owner == cell || (neighbour.has_value() && *neighbour == cell)));
        }
    }
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        CHECK(appearances[f] == (topology.isInternal(FaceIndex::fromValue(f)) ? 2U : 1U));
    }
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

} // namespace

// ---------------------------------------------------------------------------
// The index types are distinct, and zero is valid
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_TheIndexTypesAreDistinctAndZeroIsAValidIndex", "[cfd][topology]") {
    // ADR-064's inversion, asserted: zero is a VALID subscript here, unlike every
    // mesh handle and document ID in BetterCAD, and a default-constructed index is
    // NOT index 0. That is why these are strong types; the compile-fail group
    // cfdtopo proves the conversions do not exist.
    CHECK(CellIndex::fromValue(0).isValid());
    CHECK(FaceIndex::fromValue(0).isValid());
    CHECK(BoundaryFaceIndex::fromValue(0).isValid());
    CHECK(PatchIndex::fromValue(0).isValid());

    CHECK_FALSE(CellIndex{}.isValid());
    CHECK_FALSE(FaceIndex{}.isValid());
    CHECK_FALSE(BoundaryFaceIndex{}.isValid());
    CHECK_FALSE(PatchIndex{}.isValid());
    CHECK(CellIndex{} != CellIndex::fromValue(0));

    // Four types, not four names for one. Nothing converts, in either direction,
    // and none is an integer.
    static_assert(!std::is_convertible_v<CellIndex, FaceIndex>);
    static_assert(!std::is_convertible_v<FaceIndex, CellIndex>);
    static_assert(!std::is_convertible_v<BoundaryFaceIndex, FaceIndex>);
    static_assert(!std::is_convertible_v<PatchIndex, CellIndex>);
    static_assert(!std::is_convertible_v<CellIndex, std::size_t>);
    static_assert(!std::is_convertible_v<std::size_t, CellIndex>);
    static_assert(!std::is_convertible_v<ElementId, CellIndex>);
    static_assert(!std::is_convertible_v<CellIndex, ElementId>);
    // Ordered, so they can key a sorted container deterministically.
    CHECK(CellIndex::fromValue(0) < CellIndex::fromValue(1));
    CHECK(FaceIndex::fromValue(2) > FaceIndex::fromValue(1));
}

// ---------------------------------------------------------------------------
// Every P16 reference mesh yields a topology
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_EveryP16ReferenceMeshYieldsATopology", "[cfd][topology]") {
    // THE MILESTONE'S CENTRAL CLAIM, measured on every body P16 qualified: the
    // compact topology is derivable from the mesh alone, and its census agrees
    // both with the mesh contract's census and with an INDEPENDENT incidence.
    // RM-MESH-08, the open profile, has no body and so no mesh.
    std::size_t meshed = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        MeshedFluid fluid(info.kind);
        const Result<cfd::CfdModel> model = fluid.prepare();
        INFO((model.has_value() ? std::string{} : model.error().message));
        REQUIRE(model.has_value());
        const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
        REQUIRE(contract.has_value());
        ++meshed;

        const Result<CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());

        const meshing::Mesh& mesh = model->mesh().mesh();
        const cfd::CfdMeshCensus& proved = contract->census();

        // The topology's census and the contract's are the same counts.
        CHECK(topology->cells() == proved.cells);
        CHECK(topology->internalFaces() == proved.interiorFaces);
        CHECK(topology->boundaryFaces() == proved.boundaryFaces);
        CHECK(topology->faces() == proved.interiorFaces + proved.boundaryFaces);
        CHECK(topology->patches() == model->map().faces().size());
        CHECK(topology->census().boundaryTriangles == mesh.triangles().size());

        // The two identities a closed Tet4 domain must satisfy.
        CHECK(4 * topology->cells() == 2 * topology->internalFaces() + topology->boundaryFaces());
        CHECK(topology->boundaryFaces() == mesh.triangles().size());

        // An independent census, written for P16's own qualification, agrees.
        const meshref::Incidence incidence = meshref::faceIncidence(mesh);
        CHECK(incidence.consistent());
        CHECK(topology->internalFaces() == incidence.interiorFaces);
        CHECK(topology->boundaryFaces() == incidence.boundaryFaces);

        // Source binding: the stamp, never the counts.
        CHECK(topology->describes(mesh));
        CHECK(topology->mesh() == mesh.stamp());
        CHECK(topology->geometry() == model->geometryRevision());

        // EVERY STRUCTURAL INVARIANT, ON EVERY MODEL: the owner rule, the winding,
        // the face/owner containment, the boundary winding against P16's own
        // triangle, both adjacency directions and the ascending cell order.
        checkStructure(*topology, mesh);

        // The inspection agrees with the build on a GOOD mesh, which the refusal
        // tests cannot show: buildable, no findings, no first problem.
        const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, model->map().faces());
        CHECK(inspection.buildable());
        CHECK(inspection.findings.empty());
        CHECK_FALSE(inspection.firstProblem().has_value());
        CHECK_FALSE(cfd::cfdTopologyProblem(mesh, model->map().faces()).has_value());
        CHECK(inspection.census == topology->census());

        WARN(std::format("{} {}: {} cells, {} internal faces, {} boundary faces, {} patches", info.id, info.name,
                         topology->cells(), topology->internalFaces(), topology->boundaryFaces(),
                         topology->patches()));
    }
    CHECK(meshed == 8);
}

TEST_CASE("CfdTopology_OwnerIsTheLowerCellAndEveryInternalFaceHasANeighbour", "[cfd][topology]") {
    // ADR-064 DECISION 4, asserted as the ORDER RELATION rather than by
    // re-deriving "first claimant": the owner is the lower CellIndex, which is the
    // same rule as ADR-063's first claimant in ascending ElementId order BECAUSE
    // CellIndex is assigned in that order. If this ever stopped holding, every
    // face area vector P19-GEOM-001 derives would point the other way.
    MeshedFluid fluid(MeshReferenceModelKind::Block);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(topology.has_value());
    // The mesh-facing checks for this model live in checkStructure, called from
    // the all-models sweep; this case is about the face RANGES.
    checkStructure(*topology, model->mesh().mesh());

    std::size_t internal = 0;
    for (std::size_t f = 0; f < topology->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = topology->owner(face);
        REQUIRE(owner.isValid());
        CHECK(owner.value() < topology->cells());
        const std::optional<CellIndex> neighbour = topology->neighbour(face);
        if (topology->isInternal(face)) {
            ++internal;
            REQUIRE(neighbour.has_value());
            CHECK(owner < *neighbour);
            CHECK(neighbour->value() < topology->cells());
            // The owner's element handle is the lower one too, which is what makes
            // "lower CellIndex" and "first claimant in ascending ElementId order"
            // the same rule.
            CHECK(topology->cellOf(owner) < topology->cellOf(*neighbour));
        } else {
            CHECK(topology->isBoundary(face));
            CHECK_FALSE(neighbour.has_value());
        }
    }
    CHECK(internal == topology->internalFaces());
    // INTERNAL FACES COME FIRST, as a partition of the whole range rather than a
    // spot check. An earlier draft asserted
    // `isInternal(face 0) == (internalFaces() > 0)`, which is a tautology:
    // isInternal IS `value() < internalFaces`, so for face 0 the left side
    // evaluates the right side. Counting both classes over every face, and
    // comparing with the census, cannot be satisfied that way.
    REQUIRE(topology->internalFaces() > 0);
    REQUIRE(topology->boundaryFaces() > 0);
    std::size_t internalSeen = 0;
    std::size_t boundarySeen = 0;
    bool boundaryThenInternal = false;
    for (std::size_t f = 0; f < topology->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        CHECK(topology->isInternal(face) != topology->isBoundary(face));
        if (topology->isInternal(face)) {
            internalSeen += 1;
            boundaryThenInternal = boundaryThenInternal || boundarySeen > 0;
        } else {
            boundarySeen += 1;
        }
    }
    CHECK(internalSeen == topology->internalFaces());
    CHECK(boundarySeen == topology->boundaryFaces());
    CHECK_FALSE(boundaryThenInternal); // contiguous, not interleaved
    CHECK_FALSE(topology->isInternal(FaceIndex::fromValue(topology->internalFaces())));
}

TEST_CASE("CfdTopology_EveryFaceIsWoundOutwardOfItsOwner", "[cfd][topology]") {
    // ADR-063's winding rule, checked GEOMETRICALLY and independently of the table
    // the builder used: the face normal must point AWAY from the owner's opposite
    // vertex. For an internal face that means it points owner -> neighbour, and
    // for a boundary face out of the fluid. This is the property P19-GEOM-001's
    // area vectors and the S . d > 0 condition rest on.
    MeshedFluid fluid(MeshReferenceModelKind::Cylinder);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(topology.has_value());
    const meshing::Mesh& mesh = model->mesh().mesh();

    // The structural helper carries the geometric winding check; what this case
    // adds is that the winding is checked on a CURVED body, where a flat-faced
    // block could hide a sign error, and that the aggregate margin is reported.
    //
    // NO AGGREGATE ASSERTION. An earlier draft also checked
    // `max over faces of (normal . toApex) < 0`, which asserts nothing the
    // per-face check has not already asserted -- Catch2's CHECK does not abort,
    // so every face is checked regardless. It is reported, not asserted.
    checkStructure(*topology, mesh);

    std::size_t checked = 0;
    double worstTowardOwner = -std::numeric_limits<double>::infinity();
    for (std::size_t f = 0; f < topology->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::array<NodeId, 3> nodes = topology->faceNodes(face);
        const NodeId apex = apexOf(mesh, topology->cellOf(topology->owner(face)), nodes);
        REQUIRE(apex.isValid());
        const Vec toApex = sub(si(mesh.findNode(apex)->position), si(mesh.findNode(nodes[0])->position));
        worstTowardOwner = std::max(worstTowardOwner, dotp(normalOf(mesh, nodes), toApex));
        ++checked;
    }
    CHECK(checked == topology->faces());
    WARN(std::format("worst (normal . toApex) over {} faces: {:.6e} (all must be < 0)", checked,
                     worstTowardOwner));
}

TEST_CASE("CfdTopology_CellAndFaceAdjacencyAgreeInBothDirections", "[cfd][topology]") {
    // THE TWO ADJACENCY TABLES ARE CROSS-CHECKED, so a face that named the wrong
    // cells would have to be wrong in both directions in the same way to pass.
    // Every cell has four faces; every face appears in the list of exactly its
    // owner and (if internal) its neighbour, and in no other cell's list.
    MeshedFluid fluid(MeshReferenceModelKind::ThinPlate);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(topology.has_value());

    std::vector<std::size_t> appearances(topology->faces(), 0);
    for (std::size_t c = 0; c < topology->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::span<const FaceIndex> faces = topology->cellFaces(cell);
        REQUIRE(faces.size() == 4);
        // Ascending and distinct: a tetrahedron has four different faces.
        CHECK(std::ranges::is_sorted(faces));
        CHECK(std::ranges::adjacent_find(faces) == faces.end());
        for (const FaceIndex face : faces) {
            REQUIRE(face.isValid());
            REQUIRE(face.value() < topology->faces());
            ++appearances[face.value()];
            const CellIndex owner = topology->owner(face);
            const std::optional<CellIndex> neighbour = topology->neighbour(face);
            CHECK((owner == cell || (neighbour.has_value() && *neighbour == cell)));
        }
    }
    // An internal face is listed by exactly two cells, a boundary face by one.
    for (std::size_t f = 0; f < topology->faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        CHECK(appearances[f] == (topology->isInternal(face) ? 2U : 1U));
    }
    // 4 * cells face slots in all, which is the identity from the other side.
    CHECK(2 * topology->internalFaces() + topology->boundaryFaces() == 4 * topology->cells());
}

TEST_CASE("CfdTopology_PatchesPartitionTheBoundaryFaces", "[cfd][topology]") {
    // BOUNDARY PATCH MEMBERSHIP: every boundary face belongs to exactly one patch,
    // each patch's faces are a contiguous ascending span (because the boundary
    // range is ordered by patch), and together they partition the boundary. A
    // bored body is used, so a patch with NO FaceName is present -- counted, never
    // refused, exactly as ADR-063 decided.
    MeshedFluid fluid(MeshReferenceModelKind::PlateWithHole);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(topology.has_value());

    std::vector<std::size_t> seen(topology->boundaryFaces(), 0);
    std::size_t total = 0;
    for (std::size_t p = 0; p < topology->patches(); ++p) {
        const PatchIndex patch = PatchIndex::fromValue(p);
        const std::span<const BoundaryFaceIndex> faces = topology->patchFaces(patch);
        total += faces.size();
        CHECK(std::ranges::is_sorted(faces));
        for (const BoundaryFaceIndex face : faces) {
            REQUIRE(face.value() < topology->boundaryFaces());
            ++seen[face.value()];
            // The reverse direction agrees.
            CHECK(topology->patchOf(face) == patch);
        }
        // The patch names a CAD face position, which is the map's own index.
        //
        // WEAK ON ITS OWN, and knowingly so: buildGeometryMeshMap sets
        // MappedFace::index to the face's own position, so this expected value
        // equals p and an implementation returning patch.value() would pass. The
        // case below, on a fixture whose indices are deliberately NOT p, is what
        // actually pins patchFace.
        REQUIRE(topology->patchFace(patch).has_value());
        CHECK(*topology->patchFace(patch) == model->map().faces()[p].index);
        // And it holds exactly the facets the map attributed to it.
        CHECK(faces.size() == model->map().faces()[p].facets.size());
    }
    CHECK(total == topology->boundaryFaces());
    CHECK(std::ranges::all_of(seen, [](std::size_t n) { return n == 1; }));
}

TEST_CASE("CfdTopology_TheIndexTranslationsAreMutualInverses", "[cfd][topology]") {
    // The three translations across the two addressing spaces, both ways.
    MeshedFluid fluid(MeshReferenceModelKind::Block);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(topology.has_value());
    const meshing::Mesh& mesh = model->mesh().mesh();

    for (std::size_t c = 0; c < topology->cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const ElementId element = topology->cellOf(cell);
        REQUIRE(element.isValid());
        const std::optional<CellIndex> back = topology->cellIndexOf(element);
        REQUIRE(back.has_value());
        CHECK(*back == cell);
    }
    // And the cells are the mesh's tetrahedra, in the mesh's own order.
    std::size_t slot = 0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        CHECK(topology->cellOf(CellIndex::fromValue(slot++)) == tet.id);
    }

    for (std::size_t b = 0; b < topology->boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundary = BoundaryFaceIndex::fromValue(b);
        const ElementId triangle = topology->triangleOf(boundary);
        REQUIRE(triangle.isValid());
        const std::optional<BoundaryFaceIndex> back = topology->boundaryFaceOfTriangle(triangle);
        REQUIRE(back.has_value());
        CHECK(*back == boundary);
        // face <-> boundary face
        const FaceIndex face = topology->faceOf(boundary);
        REQUIRE(topology->isBoundary(face));
        const std::optional<BoundaryFaceIndex> again = topology->boundaryFaceOf(face);
        REQUIRE(again.has_value());
        CHECK(*again == boundary);
        // The triangle's nodes are the face's nodes, as a set.
        std::array<NodeId, 3> faceNodes = topology->faceNodes(face);
        std::ranges::sort(faceNodes);
        const auto found = std::ranges::find_if(
            mesh.triangles(), [triangle](const meshing::Triangle& t) { return t.id == triangle; });
        REQUIRE(found != mesh.triangles().end());
        std::array<NodeId, 3> triangleNodes = found->nodes;
        std::ranges::sort(triangleNodes);
        CHECK(faceNodes == triangleNodes);
    }
    // An internal face has no boundary index.
    if (topology->internalFaces() > 0) {
        CHECK_FALSE(topology->boundaryFaceOf(FaceIndex::fromValue(0)).has_value());
    }
}

// ---------------------------------------------------------------------------
// Sparse handles, and no array sized by one
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_IndexesCellsWhoseElementHandlesAreSparse", "[cfd][topology]") {
    // ADR-064 DECISION 5. Tetrahedra and triangles SHARE ONE ELEMENT ID SPACE
    // (ADR-031), so adding a boundary triangle between two tetrahedra makes the
    // tetrahedra's own handles non-contiguous -- which is the realistic sparsity,
    // not a contrived one. The topology must index by count and translate by
    // search.
    meshing::MeshBuilder builder;
    std::vector<NodeId> n;
    for (const Point3D& p : twoCellPoints()) {
        n.push_back(require(builder.addNode(p)));
    }
    // Cell A, then one of its boundary triangles, then cell B: A and B are no
    // longer consecutive element handles.
    REQUIRE(builder.addTetrahedron({n[0], n[1], n[2], n[3]}, kRegion)); // A
    REQUIRE(builder.addTriangle({n[0], n[2], n[1]}, kRegion));          // A's face {0,2,1}
    REQUIRE(builder.addTetrahedron({n[1], n[2], n[3], n[4]}, kRegion)); // B
    REQUIRE(builder.addTriangle({n[0], n[1], n[3]}, kRegion));
    REQUIRE(builder.addTriangle({n[0], n[3], n[2]}, kRegion));
    REQUIRE(builder.addTriangle({n[1], n[2], n[4]}, kRegion));
    REQUIRE(builder.addTriangle({n[2], n[3], n[4]}, kRegion));
    REQUIRE(builder.addTriangle({n[1], n[4], n[3]}, kRegion));
    const meshing::Mesh mesh = builder.build();

    REQUIRE(mesh.tetrahedra().size() == 2);
    // The sparsity this test exists for.
    const ElementId first = mesh.tetrahedra()[0].id;
    const ElementId second = mesh.tetrahedra()[1].id;
    CHECK(second.value() > first.value() + 1);

    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());

    CHECK(topology->cells() == 2);
    CHECK(topology->internalFaces() == 1);
    CHECK(topology->boundaryFaces() == 6);
    CHECK(4 * topology->cells() == 2 * topology->internalFaces() + topology->boundaryFaces());

    // Both sparse handles resolve, and to the right cells.
    REQUIRE(topology->cellIndexOf(first).has_value());
    REQUIRE(topology->cellIndexOf(second).has_value());
    CHECK(*topology->cellIndexOf(first) == CellIndex::fromValue(0));
    CHECK(*topology->cellIndexOf(second) == CellIndex::fromValue(1));

    // A handle in the gap is a triangle, not a cell, and must not resolve.
    for (ElementId::ValueType v = first.value() + 1; v < second.value(); ++v) {
        CHECK_FALSE(topology->cellIndexOf(ElementId::fromValue(v)).has_value());
    }
    // NO ARRAY IS SIZED BY A HANDLE'S VALUE. A lookup far beyond every handle the
    // mesh ever issued must answer "no", not read past the end of something: an
    // implementation that indexed a vector by ElementId::value() would be caught
    // here by the Debug build's bounds checking, and would have allocated four
    // billion slots for two cells.
    CHECK_FALSE(topology->cellIndexOf(ElementId::fromValue(900000)).has_value());
    CHECK_FALSE(topology->cellIndexOf(ElementId::fromValue(4000000000U)).has_value());
    CHECK_FALSE(topology->boundaryFaceOfTriangle(ElementId::fromValue(4000000000U)).has_value());
    CHECK_FALSE(topology->cellIndexOf(ElementId{}).has_value());
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_TwoBuildsOfOneMeshAreEqual", "[cfd][topology]") {
    // DETERMINISM, as equality of the whole value rather than of a count: the
    // faces are keyed in an ordered map, the cells walked in the mesh's own order,
    // and both sort keys are total, so there is nothing for an unordered
    // container or an address to leak into.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> patches = patchesOf(mesh, 3);
    const Result<CfdTopology> first = cfd::buildCfdTopology(mesh, patches);
    const Result<CfdTopology> second = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    CHECK(first->census() == second->census());

    // And on a real P16 mesh, where the cell count is not two.
    MeshedFluid fluid(MeshReferenceModelKind::Tube);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> a = cfd::requireCfdTopology(*model, *contract);
    const Result<CfdTopology> b = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
}

TEST_CASE("CfdTopology_InternalFacesAreOrderedByOwnerThenNeighbour", "[cfd][topology]") {
    // ADR-064 DECISION 3's order, asserted. The matrix assembly P19 will add sweeps
    // this range, and the order is what makes its off-diagonal entries row-major.
    MeshedFluid fluid(MeshReferenceModelKind::LocalRefinement);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(topology.has_value());
    REQUIRE(topology->internalFaces() > 1);

    for (std::size_t f = 1; f < topology->internalFaces(); ++f) {
        const FaceIndex previous = FaceIndex::fromValue(f - 1);
        const FaceIndex current = FaceIndex::fromValue(f);
        const CellIndex ownerPrevious = topology->owner(previous);
        const CellIndex ownerCurrent = topology->owner(current);
        CHECK(ownerPrevious <= ownerCurrent);
        if (ownerPrevious == ownerCurrent) {
            CHECK(*topology->neighbour(previous) < *topology->neighbour(current));
        }
    }
    // The boundary range is grouped by patch: the patch index never decreases.
    for (std::size_t b = 1; b < topology->boundaryFaces(); ++b) {
        CHECK(topology->patchOf(BoundaryFaceIndex::fromValue(b - 1)) <=
              topology->patchOf(BoundaryFaceIndex::fromValue(b)));
    }
}

// ---------------------------------------------------------------------------
// The six refusals, each on a mesh built to fail it
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_RefusesAFaceSharedByThreeCells", "[cfd][topology]") {
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kThreeOnAFace);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    CHECK_FALSE(inspection.buildable());
    CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::NonManifoldFace});
    REQUIRE(inspection.findings.size() == 1);
    CHECK(inspection.findings.front().count == 1);
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("shared by 3 cells"));
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("no single neighbour"));
    CHECK(cfd::cfdTopologyProblem(mesh, patches) == CfdTopologyProblem::NonManifoldFace);
    CHECK_FALSE(cfd::buildCfdTopology(mesh, patches).has_value());
}

TEST_CASE("CfdTopology_RefusesAFaceWithNoSecondCellAndNoBoundaryTriangle", "[cfd][topology]") {
    // The checklist's "missing neighbours" AND its "internal faces with one
    // owner": one condition from two sides, refused once (ADR-064, Decision 7).
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells, Spoil{.omitOneTriangle = true});
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    CHECK_FALSE(inspection.buildable());
    CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::MissingNeighbour});
    CHECK(inspection.findings.front().count == 1);
    CHECK_THAT(inspection.findings.front().message,
               ContainsSubstring("no second cell and no boundary triangle"));
    CHECK_FALSE(cfd::buildCfdTopology(mesh, patches).has_value());
}

TEST_CASE("CfdTopology_RefusesABoundaryTriangleThatIsNoFaceOfAnyCell", "[cfd][topology]") {
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells, Spoil{.strayTriangle = true});
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    CHECK_FALSE(inspection.buildable());
    CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::UnownedBoundaryFace});
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("is a face of no cell"));
    CHECK_FALSE(cfd::buildCfdTopology(mesh, patches).has_value());
}

TEST_CASE("CfdTopology_RefusesABoundaryTriangleOnAFaceTwoCellsShare", "[cfd][topology]") {
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells, Spoil{.coverInteriorFace = true});
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    CHECK_FALSE(inspection.buildable());
    CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::BoundaryFaceWithTwoOwners});
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("which 2 cells share"));
    CHECK_FALSE(cfd::buildCfdTopology(mesh, patches).has_value());
}

TEST_CASE("CfdTopology_RefusesTwoBoundaryTrianglesOnOneFace", "[cfd][topology]") {
    // The checklist's "duplicate internal faces", satisfied more narrowly than the
    // wording and deliberately so: the internal table is keyed by the sorted node
    // triple, so a duplicate CANNOT arise there. The reachable duplicate is a
    // boundary one (ADR-064, Decision 7).
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells, Spoil{.duplicateTriangle = true});
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    CHECK_FALSE(inspection.buildable());
    CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::DuplicateBoundaryFace});
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("claim the same face"));
    CHECK_FALSE(cfd::buildCfdTopology(mesh, patches).has_value());
}

TEST_CASE("CfdTopology_RefusesABoundaryTriangleAttributedToNoPatchOrToSeveral", "[cfd][topology]") {
    // Not on the checklist and refused anyway: patchOf must be total, and giving an
    // unattributed facet to the nearest CAD face is the silent rebind ADR-032
    // forbids.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);

    SECTION("no patch") {
        std::vector<meshing::MappedFace> patches = onePatch(mesh);
        REQUIRE(patches.front().facets.size() > 1);
        patches.front().facets.pop_back();
        const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
        CHECK_FALSE(inspection.buildable());
        CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::UnpatchedBoundaryFace});
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("attributed to no patch"));
    }

    SECTION("two patches") {
        std::vector<meshing::MappedFace> patches = patchesOf(mesh, 2);
        // The first patch's first facet is given to the second patch as well.
        patches[1].facets.push_back(patches[0].facets.front());
        const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
        CHECK_FALSE(inspection.buildable());
        CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::UnpatchedBoundaryFace});
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("attributed to patches"));
    }
}

TEST_CASE("CfdTopology_TheRefusalNamesTheControlAndEveryFindingInOrder", "[cfd][topology]") {
    // Several defects at once, reported in kCfdTopologyProblems order, with the
    // meshing control named -- the shape ADR-063's refusal established.
    const meshing::Mesh mesh =
        buildMesh(twoCellPoints(), kTwoCells, Spoil{.omitOneTriangle = true, .strayTriangle = true});
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    REQUIRE_FALSE(inspection.buildable());
    // Declaration order, not discovery order.
    CHECK(problemsOf(inspection) ==
          std::vector{CfdTopologyProblem::MissingNeighbour, CfdTopologyProblem::UnownedBoundaryFace});
    CHECK(inspection.firstProblem() == CfdTopologyProblem::MissingNeighbour);

    const Error refusal = cfd::cfdTopologyRefusal(inspection, MeshControlId::fromValue(7));
    CHECK(refusal.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refusal.message, ContainsSubstring("meshing control 7"));
    CHECK_THAT(refusal.message, ContainsSubstring("does not yield a finite-volume topology"));
    CHECK_THAT(refusal.message, ContainsSubstring("missing_neighbour"));
    CHECK_THAT(refusal.message, ContainsSubstring("unowned_boundary_face"));
    // Both findings, joined, in order.
    const auto missing = refusal.message.find("missing_neighbour");
    const auto unowned = refusal.message.find("unowned_boundary_face");
    CHECK(missing < unowned);

    // Every problem has a distinct, stable name.
    std::set<std::string_view> names;
    for (const CfdTopologyProblem problem : cfd::kCfdTopologyProblems) {
        names.insert(cfd::toString(problem));
        CHECK(cfd::toString(problem) != "unknown");
    }
    CHECK(names.size() == cfd::kCfdTopologyProblems.size());
}

// ---------------------------------------------------------------------------
// The staged gate and its stamp binding
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_RefusesAContractProvedForAnotherMeshGeneration", "[cfd][topology]") {
    // THE WHOLE REASON CfdMeshContract CARRIES A STAMP (ADR-063). A contract proved
    // for one generation says nothing about the next, however equal their counts,
    // and this gate must refuse rather than reinterpret.
    MeshedFluid first(MeshReferenceModelKind::Block);
    const Result<cfd::CfdModel> firstModel = first.prepare();
    REQUIRE(firstModel.has_value());
    const Result<cfd::CfdMeshContract> firstContract = cfd::requireCfdMesh(*firstModel);
    REQUIRE(firstContract.has_value());

    // A second, independently meshed copy of the SAME body: the same counts, a
    // different generation.
    MeshedFluid second(MeshReferenceModelKind::Block);
    const Result<cfd::CfdModel> secondModel = second.prepare();
    REQUIRE(secondModel.has_value());
    const Result<cfd::CfdMeshContract> secondContract = cfd::requireCfdMesh(*secondModel);
    REQUIRE(secondContract.has_value());

    REQUIRE(firstContract->mesh() != secondContract->mesh());
    CHECK(firstContract->census() == secondContract->census()); // equal counts, as promised

    // Each contract works with its own model.
    CHECK(cfd::requireCfdTopology(*firstModel, *firstContract).has_value());
    CHECK(cfd::requireCfdTopology(*secondModel, *secondContract).has_value());

    // And is refused against the other's, by STAMP and not by counts.
    const Result<CfdTopology> crossed = cfd::requireCfdTopology(*secondModel, *firstContract);
    REQUIRE_FALSE(crossed.has_value());
    CHECK(crossed.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(crossed.error().message, ContainsSubstring("was proved for mesh generation"));
    CHECK_THAT(crossed.error().message, ContainsSubstring("says nothing about"));
}

// ---------------------------------------------------------------------------
// Totality
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_EveryAccessorIsTotalForAnIndexItDoesNotHold", "[cfd][topology]") {
    // No accessor has a precondition a caller can violate into undefined
    // behaviour: an index this topology does not hold yields an invalid index, an
    // empty optional or an empty span.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());

    const FaceIndex pastEnd = FaceIndex::fromValue(topology->faces());
    CHECK_FALSE(topology->owner(pastEnd).isValid());
    CHECK_FALSE(topology->neighbour(pastEnd).has_value());
    CHECK(topology->faceNodes(pastEnd) == std::array<NodeId, 3>{});
    CHECK_FALSE(topology->isInternal(pastEnd));
    CHECK_FALSE(topology->isBoundary(pastEnd));
    CHECK_FALSE(topology->boundaryFaceOf(pastEnd).has_value());

    CHECK_FALSE(topology->owner(FaceIndex{}).isValid());
    CHECK(topology->cellFaces(CellIndex{}).empty());
    CHECK(topology->cellFaces(CellIndex::fromValue(topology->cells())).empty());
    CHECK_FALSE(topology->cellOf(CellIndex{}).isValid());
    CHECK_FALSE(topology->cellOf(CellIndex::fromValue(topology->cells())).isValid());

    CHECK(topology->patchFaces(PatchIndex{}).empty());
    CHECK(topology->patchFaces(PatchIndex::fromValue(topology->patches())).empty());
    CHECK_FALSE(topology->patchOf(BoundaryFaceIndex{}).isValid());
    CHECK_FALSE(topology->patchOf(BoundaryFaceIndex::fromValue(topology->boundaryFaces())).isValid());
    CHECK_FALSE(topology->triangleOf(BoundaryFaceIndex::fromValue(topology->boundaryFaces())).isValid());
    CHECK_FALSE(topology->faceOf(BoundaryFaceIndex::fromValue(topology->boundaryFaces())).isValid());
    CHECK_FALSE(topology->faceOf(BoundaryFaceIndex{}).isValid());
}

TEST_CASE("CfdTopology_AnEmptyMeshYieldsAnEmptyTopologyAndIsNotTheContractsRefusal", "[cfd][topology]") {
    // The topology does NOT re-refuse an empty mesh: EmptyMesh is the mesh
    // contract's refusal (ADR-063), and requireCfdTopology cannot be reached
    // without a contract, so a second copy of that rule would be a second thing to
    // keep true. Recorded here so the choice is visible rather than accidental.
    meshing::MeshBuilder builder;
    const meshing::Mesh mesh = builder.build();
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, {});
    REQUIRE(topology.has_value());
    CHECK(topology->cells() == 0);
    CHECK(topology->faces() == 0);
    CHECK(topology->patches() == 0);
    CHECK(topology->describes(mesh));
}

// ---------------------------------------------------------------------------
// The two refusals the winding guarantee depends on
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_RefusesAnInvertedCellBecauseItsFacesAreNotWoundOutward", "[cfd][topology]") {
    // THE DEFECT AN ADVERSARIAL REVIEW FOUND. faceNodes promises every face is
    // wound outward of its owner, and P19-GEOM-001 is told to build its area
    // vectors on that promise -- but kTetFaces is outward only of a POSITIVELY
    // oriented tetrahedron, and nothing here checked the volume. A plain-built
    // topology of an inverted cell therefore satisfied its own invariant check
    // while every face pointed the wrong way. The promise is now enforced.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kInvertedCell);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    REQUIRE_FALSE(inspection.buildable());
    CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::NonPositiveCellVolume});
    CHECK(inspection.findings.front().count == 1);
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("signed volume"));
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("not wound outward of it"));
    CHECK_FALSE(cfd::buildCfdTopology(mesh, patches).has_value());

    // And the positively oriented counterpart of the same four nodes builds.
    const meshing::Mesh good = buildMesh(twoCellPoints(), {{0, 1, 2, 3}});
    const Result<CfdTopology> built = cfd::buildCfdTopology(good, onePatch(good));
    REQUIRE(built.has_value());
    CHECK(built->cells() == 1);
    CHECK(built->boundaryFaces() == 4);
    checkStructure(*built, good);
}

TEST_CASE("CfdTopology_RefusesTwoCellsFoldedOntoOneSideOfTheirSharedFace", "[cfd][topology]") {
    // THE OTHER HALF OF THE SAME DEFECT, and the reason a volume check alone is
    // not enough: both cells here have POSITIVE volume, yet they lie on the same
    // side of the face they share, so the owner's winding does not point at the
    // neighbour and "normal points owner -> neighbour" would be false.
    const meshing::Mesh mesh = buildMesh(foldedPoints(), kFoldedCells);
    const std::vector<meshing::MappedFace> patches = onePatch(mesh);
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
    REQUIRE_FALSE(inspection.buildable());
    CHECK(problemsOf(inspection) == std::vector{CfdTopologyProblem::InconsistentFaceOrientation});
    CHECK(inspection.findings.front().count == 1);
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("in the same direction"));
    CHECK_THAT(inspection.findings.front().message, ContainsSubstring("owner to neighbour"));
    CHECK_FALSE(cfd::buildCfdTopology(mesh, patches).has_value());
    // Not a volume problem: that is what makes this check necessary on its own.
    const std::vector<CfdTopologyProblem> problems = problemsOf(inspection);
    CHECK(std::ranges::find(problems, CfdTopologyProblem::NonPositiveCellVolume) == problems.end());
}

// ---------------------------------------------------------------------------
// Cases added after a test-gap review. Each closes a gap that review named.
// ---------------------------------------------------------------------------

TEST_CASE("CfdTopology_PatchFaceNamesTheCadFaceAndNotThePatchIndex", "[cfd][topology]") {
    // THE TAUTOLOGY THIS EXISTS TO BREAK. In a real map, MappedFace::index equals
    // the face's own position, so `patchFace(p) == map.faces()[p].index` is
    // `p == p` and would pass for an implementation that returned patch.value()
    // and never read the stored value. `patchesOf` sets index = 100 + p for
    // exactly this reason; until this case, nothing used it with patchFace.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::vector<meshing::MappedFace> patches = patchesOf(mesh, 3);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());
    REQUIRE(topology->patches() == 3);
    for (std::size_t p = 0; p < 3; ++p) {
        const PatchIndex patch = PatchIndex::fromValue(p);
        REQUIRE(topology->patchFace(patch).has_value());
        CHECK(*topology->patchFace(patch) == 100 + p);
        CHECK(*topology->patchFace(patch) != p);
    }
    // And the refusal arm, whose returned value was previously an unpinned
    // SIZE_MAX sentinel -- the very thing this file's own comments reject -- and
    // is now simply absent.
    CHECK_FALSE(topology->patchFace(PatchIndex{}).has_value());
    CHECK_FALSE(topology->patchFace(PatchIndex::fromValue(3)).has_value());
    CHECK_FALSE(topology->triangleOf(BoundaryFaceIndex{}).isValid());
}

TEST_CASE("CfdTopology_EqualityDiscriminatesSoDeterminismMeansSomething", "[cfd][topology]") {
    // WITHOUT THIS, THE DETERMINISM ITEM IS VACUOUS. Determinism is asserted as
    // `*a == *b`, which an operator== that compared nothing would satisfy for
    // every pair. So equality must be shown to separate topologies that differ.
    const meshing::Mesh twoCells = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> one = cfd::buildCfdTopology(twoCells, onePatch(twoCells));
    const Result<CfdTopology> three = cfd::buildCfdTopology(twoCells, patchesOf(twoCells, 3));
    REQUIRE(one.has_value());
    REQUIRE(three.has_value());
    // The same mesh under a different patch partition: the same faces, different
    // patch membership, and therefore a different boundary order.
    CHECK(*one != *three);
    CHECK(one->census() != three->census());

    // And two different meshes.
    MeshedFluid block(MeshReferenceModelKind::Block);
    const Result<cfd::CfdModel> blockModel = block.prepare();
    REQUIRE(blockModel.has_value());
    const Result<cfd::CfdMeshContract> blockContract = cfd::requireCfdMesh(*blockModel);
    REQUIRE(blockContract.has_value());
    const Result<CfdTopology> blockTopology = cfd::requireCfdTopology(*blockModel, *blockContract);
    REQUIRE(blockTopology.has_value());
    CHECK(*blockTopology != *one);
}

TEST_CASE("CfdTopology_AReorderedPatchFacetListYieldsTheSameTopology", "[cfd][topology]") {
    // DETERMINISM UNDER A PERMUTED INPUT, which the two-builds check cannot show:
    // the face order must come from the mesh's own cell order and the patch
    // numbering, not from the order the map happens to list facets in.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    std::vector<meshing::MappedFace> forward = patchesOf(mesh, 2);
    std::vector<meshing::MappedFace> reversed = forward;
    for (meshing::MappedFace& patch : reversed) {
        std::ranges::reverse(patch.facets);
    }
    const Result<CfdTopology> a = cfd::buildCfdTopology(mesh, forward);
    const Result<CfdTopology> b = cfd::buildCfdTopology(mesh, reversed);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
}

TEST_CASE("CfdTopology_DescribesIsFalseForALaterGenerationOfTheSameMesh", "[cfd][topology]") {
    // "Exact mesh-source binding" was only half substantiated: describes() was
    // asserted TRUE and never FALSE, and no case took a topology across a remesh
    // of the SAME document -- which is the situation the binding exists for.
    MeshedFluid fluid(MeshReferenceModelKind::ThinPlate);
    const Result<cfd::CfdModel> first = fluid.prepare();
    REQUIRE(first.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*first);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> topology = cfd::requireCfdTopology(*first, *contract);
    REQUIRE(topology.has_value());
    const meshing::MeshStamp firstStamp = topology->mesh();
    CHECK(topology->describes(first->mesh().mesh()));

    // RE-PREPARING THE SAME DOCUMENT DOES NOT REMESH. requireCfdModel reads the
    // generation the mesher already holds, so the stamp is unchanged and the
    // topology still describes it. Asserted because the first draft of this case
    // assumed the opposite and failed: a topology is not silently invalidated by
    // preparing the input again.
    const Result<cfd::CfdModel> again = fluid.prepare();
    REQUIRE(again.has_value());
    CHECK(again->mesh().mesh().stamp() == firstStamp);
    CHECK(topology->describes(again->mesh().mesh()));

    // A SEPARATE GENERATION of the same model: the same counts, a different
    // stamp. This is what the binding exists to refuse, and it is refused on the
    // stamp alone -- the censuses below are equal.
    MeshedFluid other(MeshReferenceModelKind::ThinPlate);
    const Result<cfd::CfdModel> otherModel = other.prepare();
    REQUIRE(otherModel.has_value());
    const meshing::Mesh& later = otherModel->mesh().mesh();
    REQUIRE(later.stamp() != firstStamp);
    const Result<cfd::CfdMeshContract> otherContract = cfd::requireCfdMesh(*otherModel);
    REQUIRE(otherContract.has_value());
    const Result<CfdTopology> otherTopology = cfd::requireCfdTopology(*otherModel, *otherContract);
    REQUIRE(otherTopology.has_value());
    CHECK(topology->census() == otherTopology->census()); // equal counts, as promised

    CHECK_FALSE(topology->describes(later));
    CHECK(topology->mesh() != later.stamp());
    CHECK_FALSE(otherTopology->describes(first->mesh().mesh()));
}

TEST_CASE("CfdTopology_ThePlainBuilderCarriesNoGeometryRevision", "[cfd][topology]") {
    // buildCfdTopology takes a plain mesh and has no geometry to name, so its
    // topology's GeometryRevision is deliberately invalid. Asserted so that one
    // from the plain builder can never be mistaken for one from the gate.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const Result<CfdTopology> plain = cfd::buildCfdTopology(mesh, onePatch(mesh));
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->geometry().isValid());
    CHECK(plain->describes(mesh)); // the MESH stamp is real; only the geometry is absent

    MeshedFluid fluid(MeshReferenceModelKind::Block);
    const Result<cfd::CfdModel> model = fluid.prepare();
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<CfdTopology> gated = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(gated.has_value());
    CHECK(gated->geometry().isValid());
}

TEST_CASE("CfdTopology_APatchWithNoFacetsIsCountedAndItsSpanIsEmpty", "[cfd][topology]") {
    // A CAD face the mesher gave no triangles is a legal input, and the patch CSR
    // must handle an empty range. Nothing exercised it: onePatch gives one patch
    // and patchesOf deals round-robin, so every patch always had a facet.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);
    const std::size_t triangles = mesh.triangles().size();
    REQUIRE(triangles == 6);
    // Two more patches than there are facets: the surplus get none.
    const std::vector<meshing::MappedFace> patches = patchesOf(mesh, triangles + 2);
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
    REQUIRE(topology.has_value());
    CHECK(topology->patches() == triangles + 2);

    std::size_t empty = 0;
    std::size_t total = 0;
    std::vector<std::size_t> seen(topology->boundaryFaces(), 0);
    for (std::size_t p = 0; p < topology->patches(); ++p) {
        const std::span<const BoundaryFaceIndex> faces = topology->patchFaces(PatchIndex::fromValue(p));
        total += faces.size();
        empty += faces.empty() ? 1U : 0U;
        for (const BoundaryFaceIndex face : faces) {
            REQUIRE(face.value() < topology->boundaryFaces());
            ++seen[face.value()];
        }
    }
    CHECK(empty == 2);
    CHECK(total == topology->boundaryFaces());
    // The partition still holds: every boundary face in exactly one patch.
    CHECK(std::ranges::all_of(seen, [](std::size_t n) { return n == 1; }));
}

TEST_CASE("CfdTopology_TwoCellsOnTheSameFourNodesShareFourFacesNotEight", "[cfd][topology]") {
    // ADR-064 ARGUES that the internal table cannot hold a duplicate, because
    // faces are keyed by their sorted node triple. An argument is not a test, so
    // here it is measured: two tetrahedra on the same four nodes claim each of
    // the four faces twice, which is FOUR faces and not eight. They are also
    // folded onto one another by definition -- each winds every shared face the
    // same way -- so the mesh is refused for the orientation, which is the
    // correct reading of a duplicated cell.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), {{0, 1, 2, 3}, {0, 1, 2, 3}});
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, onePatch(mesh));
    CHECK(inspection.census.cells == 2);
    // FOUR faces, each claimed twice: not eight, which is the impossibility.
    CHECK(inspection.census.internalFaces == 4);
    CHECK(inspection.census.boundaryFaces == 0);
    CHECK(4 * inspection.census.cells == 2 * inspection.census.internalFaces + inspection.census.boundaryFaces);
    // No duplicate was reported, because none exists to report.
    const std::vector<CfdTopologyProblem> problems = problemsOf(inspection);
    CHECK(std::ranges::find(problems, CfdTopologyProblem::DuplicateBoundaryFace) == problems.end());
    // All four shared faces are traversed the same way by both cells.
    const auto orientation = std::ranges::find_if(inspection.findings, [](const cfd::CfdTopologyFinding& f) {
        return f.problem == CfdTopologyProblem::InconsistentFaceOrientation;
    });
    REQUIRE(orientation != inspection.findings.end());
    CHECK(orientation->count == 4);
}

TEST_CASE("CfdTopology_TwoUnpatchedTrianglesAreOneFindingWithACountOfTwo", "[cfd][topology]") {
    // No finding had ever been produced with a count above 1, so the count field
    // and "keeps the FIRST occurrence" were both unexercised.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells, Spoil{.omitOneTriangle = true});
    const cfd::CfdTopologyInspection single = cfd::inspectCfdTopology(mesh, onePatch(mesh));
    REQUIRE(single.findings.size() == 1);
    CHECK(single.findings.front().count == 1);

    // Two facets withheld from the patch list give ONE finding of count 2.
    const meshing::Mesh whole = buildMesh(twoCellPoints(), kTwoCells);
    std::vector<meshing::MappedFace> patches = onePatch(whole);
    REQUIRE(patches.front().facets.size() == 6);
    const ElementId firstWithheld = patches.front().facets[4];
    patches.front().facets.resize(4);
    const cfd::CfdTopologyInspection two = cfd::inspectCfdTopology(whole, patches);
    REQUIRE(two.findings.size() == 1);
    CHECK(two.findings.front().problem == CfdTopologyProblem::UnpatchedBoundaryFace);
    CHECK(two.findings.front().count == 2);
    // The message names the FIRST occurrence in mesh order, not the last.
    CHECK_THAT(two.findings.front().message,
               ContainsSubstring(std::format("triangle {} ", firstWithheld.value())));
    CHECK_THAT(two.findings.front().message, ContainsSubstring("(2)"));
}

TEST_CASE("CfdTopology_ABoundaryTriangleOnAThreeCellFaceReportsBothProblemsInOrder", "[cfd][topology]") {
    // The `claims >= 3` arm of the triangle loop, which no fixture reached: a
    // triangle on a face three cells share is BOTH a non-manifold face and a
    // boundary face with more than one owner, and both are reported, in
    // declaration order.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kThreeOnAFace, Spoil{.coverInteriorFace = true});
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, onePatch(mesh));
    REQUIRE_FALSE(inspection.buildable());
    const std::vector<CfdTopologyProblem> problems = problemsOf(inspection);
    REQUIRE(problems.size() >= 2);
    CHECK(problems.front() == CfdTopologyProblem::NonManifoldFace);
    CHECK(std::ranges::find(problems, CfdTopologyProblem::BoundaryFaceWithTwoOwners) != problems.end());
    // "which 3 cells share", not 2: the count is the real one.
    const auto twoOwners = std::ranges::find_if(inspection.findings, [](const cfd::CfdTopologyFinding& f) {
        return f.problem == CfdTopologyProblem::BoundaryFaceWithTwoOwners;
    });
    REQUIRE(twoOwners != inspection.findings.end());
    CHECK_THAT(twoOwners->message, ContainsSubstring("which 3 cells share"));
}

TEST_CASE("CfdTopology_AFacetInSeveralPatchesIsReportedOnceAndNamesThem", "[cfd][topology]") {
    // The `alreadyReported.contains` arm: a facet named by three patches must not
    // produce one finding per extra claim. And a facet listed twice within ONE
    // patch used to report "attributed to patches 0 and 0", a correct refusal
    // with a nonsense diagnostic.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells);

    SECTION("three patches") {
        std::vector<meshing::MappedFace> patches = patchesOf(mesh, 3);
        const ElementId shared = patches[0].facets.front();
        patches[1].facets.push_back(shared);
        patches[2].facets.push_back(shared);
        const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
        REQUIRE_FALSE(inspection.buildable());
        REQUIRE(inspection.findings.size() == 1);
        CHECK(inspection.findings.front().problem == CfdTopologyProblem::UnpatchedBoundaryFace);
        CHECK(inspection.findings.front().count == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("attributed to patches 0 and 1"));
    }

    SECTION("twice in one patch") {
        std::vector<meshing::MappedFace> patches = onePatch(mesh);
        patches.front().facets.push_back(patches.front().facets.front());
        const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, patches);
        REQUIRE_FALSE(inspection.buildable());
        REQUIRE(inspection.findings.size() == 1);
        CHECK_THAT(inspection.findings.front().message, ContainsSubstring("attributed twice to patch 0"));
        CHECK_THAT(inspection.findings.front().message, !ContainsSubstring("0 and 0"));
    }
}

TEST_CASE("CfdTopology_ThePlainBuildersRefusalNamesNoControlAndJoinsEveryFinding", "[cfd][topology]") {
    // buildCfdTopology carries its own refusal message -- a second join, separate
    // from cfdTopologyRefusal's -- whose output no test had read.
    const meshing::Mesh mesh =
        buildMesh(twoCellPoints(), kTwoCells, Spoil{.omitOneTriangle = true, .strayTriangle = true});
    const Result<CfdTopology> built = cfd::buildCfdTopology(mesh, onePatch(mesh));
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(built.error().message, ContainsSubstring("the mesh does not yield a finite-volume topology"));
    // No control: a plain mesh has none to name, unlike the staged gate's refusal.
    CHECK_THAT(built.error().message, !ContainsSubstring("meshing control"));
    // Both findings, joined in declaration order.
    const auto missing = built.error().message.find("missing_neighbour");
    const auto unowned = built.error().message.find("unowned_boundary_face");
    CHECK(missing != std::string::npos);
    CHECK(unowned != std::string::npos);
    CHECK(missing < unowned);
    CHECK_THAT(built.error().message, ContainsSubstring("; "));
}

TEST_CASE("CfdTopology_ARefusedInspectionStillReportsTheCensusItClassified", "[cfd][topology]") {
    // The census of a refused inspection is documented as the diagnostic reader's
    // output and was executed by every refusal test but read by none.
    const meshing::Mesh mesh = buildMesh(twoCellPoints(), kTwoCells, Spoil{.omitOneTriangle = true});
    const cfd::CfdTopologyInspection inspection = cfd::inspectCfdTopology(mesh, onePatch(mesh));
    REQUIRE_FALSE(inspection.buildable());
    CHECK(inspection.census.cells == 2);
    CHECK(inspection.census.internalFaces == 1);
    // Five of the six boundary faces were covered; the omitted one is not counted.
    CHECK(inspection.census.boundaryFaces == 5);
    CHECK(inspection.census.faces == 6);
    CHECK(inspection.census.boundaryTriangles == 5);
    CHECK(inspection.census.patches == 1);
}

TEST_CASE("CfdTopology_SparseTriangleHandlesRoundTripAndACellHandleIsNotOne", "[cfd][topology]") {
    // The sparse case asserted cellIndexOf only. The triangle direction has the
    // same sparsity -- a tet handle sits between two triangle handles -- and the
    // mismatch arm of boundaryFaceOfTriangle's binary search was never reached.
    meshing::MeshBuilder builder;
    std::vector<NodeId> n;
    for (const Point3D& p : twoCellPoints()) {
        n.push_back(require(builder.addNode(p)));
    }
    REQUIRE(builder.addTetrahedron({n[0], n[1], n[2], n[3]}, kRegion));
    REQUIRE(builder.addTriangle({n[0], n[2], n[1]}, kRegion));
    REQUIRE(builder.addTetrahedron({n[1], n[2], n[3], n[4]}, kRegion));
    REQUIRE(builder.addTriangle({n[0], n[1], n[3]}, kRegion));
    REQUIRE(builder.addTriangle({n[0], n[3], n[2]}, kRegion));
    REQUIRE(builder.addTriangle({n[1], n[2], n[4]}, kRegion));
    REQUIRE(builder.addTriangle({n[2], n[3], n[4]}, kRegion));
    REQUIRE(builder.addTriangle({n[1], n[4], n[3]}, kRegion));
    const meshing::Mesh mesh = builder.build();
    const Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
    REQUIRE(topology.has_value());

    for (std::size_t b = 0; b < topology->boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundary = BoundaryFaceIndex::fromValue(b);
        const ElementId triangle = topology->triangleOf(boundary);
        REQUIRE(triangle.isValid());
        const std::optional<BoundaryFaceIndex> back = topology->boundaryFaceOfTriangle(triangle);
        REQUIRE(back.has_value());
        CHECK(*back == boundary);
    }
    // A CELL handle is not a triangle handle, and it sits INSIDE the triangle
    // handles' range -- so this reaches the mismatch arm of the binary search,
    // not the past-the-end arm.
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        CHECK_FALSE(topology->boundaryFaceOfTriangle(tet.id).has_value());
    }
    CHECK_FALSE(topology->boundaryFaceOfTriangle(ElementId{}).has_value());
    // And every structural invariant holds on a sparse mesh too.
    checkStructure(*topology, mesh);
}

TEST_CASE("CfdTopology_EveryProblemHasADistinctStableNameAndTheArrayIsDeclarationOrder", "[cfd][topology]") {
    // toString's fallback arm, reachable only by a cast, and the array's
    // element-by-element agreement with the enum -- which "findings are reported
    // in declaration order" rests on and which was only checked indirectly.
    CHECK(cfd::toString(static_cast<CfdTopologyProblem>(99)) == "unknown");
    CHECK(cfd::kCfdTopologyProblems.size() == 8);
    CHECK(cfd::kCfdTopologyProblems[0] == CfdTopologyProblem::NonPositiveCellVolume);
    CHECK(cfd::kCfdTopologyProblems[1] == CfdTopologyProblem::NonManifoldFace);
    CHECK(cfd::kCfdTopologyProblems[2] == CfdTopologyProblem::InconsistentFaceOrientation);
    CHECK(cfd::kCfdTopologyProblems[3] == CfdTopologyProblem::MissingNeighbour);
    CHECK(cfd::kCfdTopologyProblems[4] == CfdTopologyProblem::UnownedBoundaryFace);
    CHECK(cfd::kCfdTopologyProblems[5] == CfdTopologyProblem::BoundaryFaceWithTwoOwners);
    CHECK(cfd::kCfdTopologyProblems[6] == CfdTopologyProblem::DuplicateBoundaryFace);
    CHECK(cfd::kCfdTopologyProblems[7] == CfdTopologyProblem::UnpatchedBoundaryFace);
    CHECK(cfd::toString(CfdTopologyProblem::NonPositiveCellVolume) == "non_positive_cell_volume");
    CHECK(cfd::toString(CfdTopologyProblem::NonManifoldFace) == "non_manifold_face");
    CHECK(cfd::toString(CfdTopologyProblem::InconsistentFaceOrientation) == "inconsistent_face_orientation");
    CHECK(cfd::toString(CfdTopologyProblem::MissingNeighbour) == "missing_neighbour");
    CHECK(cfd::toString(CfdTopologyProblem::UnownedBoundaryFace) == "unowned_boundary_face");
    CHECK(cfd::toString(CfdTopologyProblem::BoundaryFaceWithTwoOwners) == "boundary_face_with_two_owners");
    CHECK(cfd::toString(CfdTopologyProblem::DuplicateBoundaryFace) == "duplicate_boundary_face");
    CHECK(cfd::toString(CfdTopologyProblem::UnpatchedBoundaryFace) == "unpatched_boundary_face");
}
