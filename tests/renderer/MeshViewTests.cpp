// P16-VIZ-001: the read-only mesh visualisation adapter.
//
// HEADLESS ON PURPOSE. Nothing here opens a view, creates a graphic driver or
// needs a GL implementation: a MeshView is buffers and two lookup tables, so
// the substance of this milestone -- counts, identity translation, the
// revision guard, determinism and non-mutation -- is asserted without a
// display. The OCCT and Qt layers are tested separately, and a machine with no
// OpenGL can still run everything in this file.
//
// WHAT THESE TESTS ARE FOR. The adapter's whole reason to exist is that the
// GUI must not become a second source of truth about the mesh. So the
// assertions are mostly of one shape: whatever the adapter says, ask the
// CANONICAL mesh the same question and require the same answer -- and where
// the adapter counts something, count it again by a different method.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/SurfaceMesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using meshing::ElementId;
using meshing::Mesh;
using meshing::NodeId;
using meshing::VolumeMesh;
using renderer::MeshSource;
using renderer::MeshView;

namespace {

/// A block whose three sides all differ, so no face can be mistaken for
/// another, meshed into a real Tet4 volume mesh.
struct Block {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    ObjectId profile{};

    explicit Block(Length a = 30_mm, Length b = 20_mm, Length c = 10_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, a, b);
        profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] VolumeMesh mesh(const meshing::VolumeMeshControls& controls = {}) {
        Result<VolumeMesh> result = meshing::volumeMeshFor(document, regenerator, feature, controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }

    void setDepth(Length depth) {
        REQUIRE(document
                    .modifyObject<features::ExtrudeFeature>(
                        feature,
                        [depth](features::ExtrudeFeature& extrude) -> Result<bool> {
                            auto definition = extrude.definition();
                            definition.depth = depth;
                            return extrude.setDefinition(definition).has_value();
                        })
                    .has_value());
        requireReport(regenerator, document);
    }
};

/// Everything about a mesh that rendering must not change. Compared whole, so
/// a new field in Mesh does not quietly fall outside the fingerprint.
struct MeshFingerprint {
    meshing::MeshStamp stamp{};
    std::vector<meshing::Node> nodes{};
    std::vector<meshing::Triangle> triangles{};
    std::vector<meshing::Tetrahedron> tetrahedra{};

    explicit MeshFingerprint(const Mesh& mesh)
        : stamp(mesh.stamp()), nodes(mesh.nodes().begin(), mesh.nodes().end()),
          triangles(mesh.triangles().begin(), mesh.triangles().end()),
          tetrahedra(mesh.tetrahedra().begin(), mesh.tetrahedra().end()) {}

    friend bool operator==(const MeshFingerprint&, const MeshFingerprint&) = default;
};

/// The unique undirected edges of a mesh's triangles, counted INDEPENDENTLY of
/// the adapter: a std::set of node pairs rather than the adapter's sort and
/// unique over a vector. Two methods agreeing is worth more than one method
/// agreeing with itself.
[[nodiscard]] std::set<std::pair<std::uint32_t, std::uint32_t>> edgesOf(const Mesh& mesh) {
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::uint32_t a = triangle.nodes[corner].value();
            const std::uint32_t b = triangle.nodes[(corner + 1U) % 3U].value();
            edges.emplace(std::min(a, b), std::max(a, b));
        }
    }
    return edges;
}

[[nodiscard]] MeshView require(Result<MeshView> view) {
    if (!view.has_value()) {
        FAIL("the mesh view was refused: " << view.error().message);
    }
    return std::move(*view);
}

} // namespace

// ---------------------------------------------------------------------------
// What is drawn, and what is deliberately not
// ---------------------------------------------------------------------------

TEST_CASE("MeshView_VolumeBoundaryDrawsEveryBoundaryFacetAndNoInteriorFace",
          "[renderer][meshview][display]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshView view = require(MeshView::volumeBoundaryOf(mesh));

    // The count comes from the canonical mesh, not from a backend figure.
    CHECK(view.triangleCount() == volume.boundaryTriangleCount());
    CHECK(view.triangleCount() == mesh.triangles().size());
    CHECK(view.source() == MeshSource::VolumeBoundary);

    // AND NO INTERIOR FACE. A tetrahedron has four faces; if interior faces
    // had leaked in, the triangle count would be a multiple of the tet count
    // rather than the boundary count. This is the assertion that would catch a
    // GUI that counted face incidences for itself.
    INFO("tets " << mesh.tetrahedra().size() << ", boundary facets "
                 << volume.boundaryTriangleCount() << ", drawn " << view.triangleCount());
    REQUIRE_FALSE(mesh.tetrahedra().empty());
    CHECK(view.triangleCount() < mesh.tetrahedra().size() * 4U);
}

TEST_CASE("MeshView_DrawsOnlyTheNodesItsFacetsReference", "[renderer][meshview][display]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshView view = require(MeshView::volumeBoundaryOf(mesh));

    // A volume mesh has interior nodes. A boundary view must not upload them,
    // so vertexCount() means "boundary nodes" and not "all nodes".
    REQUIRE(mesh.nodeCount() > 0U);
    INFO("mesh nodes " << mesh.nodeCount() << ", drawn vertices " << view.vertexCount());
    CHECK(view.vertexCount() <= mesh.nodeCount());

    // Every drawn vertex is a real node of the mesh, and every position is the
    // canonical position exactly -- no re-projection, no snapping, no
    // tolerance. A renderer that moved a node to make the picture tidier would
    // hide the conformity defect it was supposed to reveal.
    for (std::size_t vertex = 0U; vertex < view.vertexCount(); ++vertex) {
        const NodeId node = require(view.nodeOfVertex(vertex));
        const meshing::Node* canonical = mesh.findNode(node);
        REQUIRE(canonical != nullptr);
        CHECK(view.positions()[vertex * 3U] == canonical->position.x.si());
        CHECK(view.positions()[(vertex * 3U) + 1U] == canonical->position.y.si());
        CHECK(view.positions()[(vertex * 3U) + 2U] == canonical->position.z.si());
    }
}

TEST_CASE("MeshView_SaysWhichCanonicalMeshItCameFrom", "[renderer][meshview][display]") {
    // The engineering surface and the volume boundary can be geometrically
    // identical -- generateVolumeMesh refuses a result whose boundary is not
    // positionally identical to the surface it was given. So the user cannot
    // tell them apart by looking, and the view has to say.
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView boundary = require(MeshView::volumeBoundaryOf(volume.mesh()));
    const MeshView surface = require(MeshView::surfaceOf(volume.mesh()));

    CHECK(boundary.source() == MeshSource::VolumeBoundary);
    CHECK(surface.source() == MeshSource::EngineeringSurface);
    CHECK(renderer::toString(boundary.source()) != renderer::toString(surface.source()));
    // Same geometry, different label: which is the whole point.
    CHECK(boundary.triangleCount() == surface.triangleCount());
}

// ---------------------------------------------------------------------------
// A render index is not an identity
// ---------------------------------------------------------------------------

TEST_CASE("MeshView_EveryRenderIndexResolvesToACanonicalIdentity",
          "[renderer][meshview][identity]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshView view = require(MeshView::volumeBoundaryOf(mesh));

    REQUIRE(view.triangleCount() > 0U);
    for (std::size_t triangle = 0U; triangle < view.triangleCount(); ++triangle) {
        const ElementId element = require(view.elementOfTriangle(triangle));
        // It resolves to a TRIANGLE of the canonical mesh -- not merely to some
        // element with that number.
        const meshing::Triangle* canonical = mesh.findTriangle(element);
        REQUIRE(canonical != nullptr);

        // And the render triangle's three vertices are that element's three
        // nodes, as a set. The winding is the canonical winding, so the order
        // matches too, but the set is what identity means here.
        std::array<NodeId, 3> drawn{};
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::uint32_t vertex = view.triangleIndices()[(triangle * 3U) + corner];
            drawn[corner] = require(view.nodeOfVertex(vertex));
        }
        CHECK(drawn == canonical->nodes);
    }
}

TEST_CASE("MeshView_ResolvesACanonicalIdentityBackToItsRenderPrimitives",
          "[renderer][meshview][identity]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshView view = require(MeshView::volumeBoundaryOf(mesh));

    // The direction highlighting needs: given an ElementId, which triangles.
    REQUIRE_FALSE(mesh.triangles().empty());
    const ElementId facet = mesh.triangles().front().id;
    const std::vector<std::size_t> triangles = view.trianglesOfElement(facet);
    REQUIRE(triangles.size() == 1U);
    CHECK(require(view.elementOfTriangle(triangles.front())) == facet);

    // And a node back to its vertex.
    const NodeId node = mesh.triangles().front().nodes[0];
    const std::size_t vertex = require(view.vertexOfNode(node));
    CHECK(require(view.nodeOfVertex(vertex)) == node);

    // An element this view does not draw answers with nothing rather than with
    // a guess. A tetrahedron is not drawn by a boundary view.
    REQUIRE_FALSE(mesh.tetrahedra().empty());
    CHECK(view.trianglesOfElement(mesh.tetrahedra().front().id).empty());
}

TEST_CASE("MeshView_RefusesAnIndexOutsideItsBuffers", "[renderer][meshview][validation]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = require(MeshView::volumeBoundaryOf(volume.mesh()));

    CHECK(errorCode(view.nodeOfVertex(view.vertexCount())) == ErrorCode::InvalidArgument);
    CHECK(errorCode(view.elementOfTriangle(view.triangleCount())) == ErrorCode::InvalidArgument);
    // A node that exists in no mesh at all.
    CHECK(errorCode(view.vertexOfNode(NodeId::fromValue(999999U))) == ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Edges
// ---------------------------------------------------------------------------

TEST_CASE("MeshView_DeduplicatesEdgesOnTheUnorderedNodePair", "[renderer][meshview][wireframe]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshView view = require(MeshView::volumeBoundaryOf(mesh));

    // Counted a second way, by a different container. Three edges per triangle
    // with every shared edge drawn twice would be 3F; the deduplicated count
    // is strictly less on any mesh where a facet has a neighbour.
    const std::size_t independent = edgesOf(mesh).size();
    CHECK(view.edgeCount() == independent);
    CHECK(view.edgeCount() < view.triangleCount() * 3U);

    // Every edge joins two distinct drawn vertices, and each pair appears once.
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    for (std::size_t edge = 0U; edge < view.edgeCount(); ++edge) {
        const std::uint32_t a = view.edgeIndices()[edge * 2U];
        const std::uint32_t b = view.edgeIndices()[(edge * 2U) + 1U];
        CHECK(a != b);
        CHECK(a < view.vertexCount());
        CHECK(b < view.vertexCount());
        CHECK(seen.emplace(std::min(a, b), std::max(a, b)).second);
    }
}

// ---------------------------------------------------------------------------
// The revision guard
// ---------------------------------------------------------------------------

TEST_CASE("MeshView_DoesNotDescribeADifferentMeshGeneration", "[renderer][meshview][revision]") {
    Block block;
    const VolumeMesh first = block.mesh();
    const MeshView view = require(MeshView::volumeBoundaryOf(first.mesh()));
    CHECK(view.describes(first.mesh()));
    CHECK(view.stamp() == first.mesh().stamp());

    // A remesh. The old view's indices now mean nothing, and it says so rather
    // than reinterpreting them against different geometry.
    block.setDepth(14_mm);
    const VolumeMesh second = block.mesh();
    REQUIRE_FALSE(second.mesh().stamp() == first.mesh().stamp());
    CHECK_FALSE(view.describes(second.mesh()));

    // The new view describes the new mesh and not the old one.
    const MeshView rebuilt = require(MeshView::volumeBoundaryOf(second.mesh()));
    CHECK(rebuilt.describes(second.mesh()));
    CHECK_FALSE(rebuilt.describes(first.mesh()));
}

// ---------------------------------------------------------------------------
// Interior inspection
// ---------------------------------------------------------------------------

TEST_CASE("MeshView_InteriorInspectionDrawsFourFacesPerTetrahedron",
          "[renderer][meshview][interior]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    REQUIRE(mesh.tetrahedra().size() >= 2U);

    const std::array<ElementId, 2> chosen{mesh.tetrahedra()[0].id, mesh.tetrahedra()[1].id};
    const MeshView view = require(MeshView::tetrahedraOf(mesh, chosen));

    CHECK(view.source() == MeshSource::SelectedTetrahedra);
    CHECK(view.triangleCount() == 8U);
    // Each chosen tetrahedron owns exactly four of the render triangles.
    for (const ElementId tet : chosen) {
        CHECK(view.trianglesOfElement(tet).size() == 4U);
    }
    // Two tetrahedra have at most 8 distinct nodes, and fewer if they share a
    // face or an edge.
    CHECK(view.vertexCount() <= 8U);
}

TEST_CASE("MeshView_TetFaceWindingsPointAwayFromTheOppositeVertex",
          "[renderer][meshview][interior]") {
    // THE PROPERTY BEHIND THE FACE TABLE, asserted rather than commented. For
    // a positively oriented Tet4 -- which is the only kind P16 accepts, since
    // signedVolume is never given an absolute value -- each face's right-hand
    // normal must point away from the vertex that face omits. If a winding in
    // the table were wrong, that face would be lit from inside and an interior
    // inspection would show a hole.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    REQUIRE_FALSE(mesh.tetrahedra().empty());

    const meshing::Tetrahedron& tet = mesh.tetrahedra().front();
    const std::array<ElementId, 1> chosen{tet.id};
    const MeshView view = require(MeshView::tetrahedraOf(mesh, chosen));
    REQUIRE(view.triangleCount() == 4U);

    const auto positionOf = [&mesh](NodeId node) {
        const meshing::Node* found = mesh.findNode(node);
        REQUIRE(found != nullptr);
        return std::array<double, 3>{found->position.x.si(), found->position.y.si(),
                                     found->position.z.si()};
    };

    // The element is positively oriented to begin with: P16 refuses inverted
    // tetrahedra, so this is a precondition and not an assumption.
    const auto p1 = positionOf(tet.nodes[0]);
    const auto p2 = positionOf(tet.nodes[1]);
    const auto p3 = positionOf(tet.nodes[2]);
    const auto p4 = positionOf(tet.nodes[3]);
    const Volume signed6 = meshing::signedVolume(
        mesh.findNode(tet.nodes[0])->position, mesh.findNode(tet.nodes[1])->position,
        mesh.findNode(tet.nodes[2])->position, mesh.findNode(tet.nodes[3])->position);
    REQUIRE(signed6.si() > 0.0);
    (void)p1;
    (void)p2;
    (void)p3;
    (void)p4;

    const std::array<std::array<double, 3>, 4> corners{p1, p2, p3, p4};
    for (std::size_t triangle = 0U; triangle < 4U; ++triangle) {
        std::array<std::array<double, 3>, 3> face{};
        std::array<NodeId, 3> faceNodes{};
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::uint32_t vertex = view.triangleIndices()[(triangle * 3U) + corner];
            faceNodes[corner] = require(view.nodeOfVertex(vertex));
            face[corner] = positionOf(faceNodes[corner]);
        }
        // The omitted vertex.
        std::size_t omitted = 4U;
        for (std::size_t which = 0U; which < 4U; ++which) {
            if (std::ranges::find(faceNodes, tet.nodes[which]) == faceNodes.end()) {
                omitted = which;
            }
        }
        REQUIRE(omitted < 4U);

        // n = (b - a) x (c - a); it must point away from the omitted corner,
        // i.e. n . (omitted - a) < 0.
        const std::array<double, 3> u{face[1][0] - face[0][0], face[1][1] - face[0][1],
                                      face[1][2] - face[0][2]};
        const std::array<double, 3> v{face[2][0] - face[0][0], face[2][1] - face[0][1],
                                      face[2][2] - face[0][2]};
        const std::array<double, 3> n{(u[1] * v[2]) - (u[2] * v[1]),
                                      (u[2] * v[0]) - (u[0] * v[2]),
                                      (u[0] * v[1]) - (u[1] * v[0])};
        const std::array<double, 3> w{corners[omitted][0] - face[0][0],
                                      corners[omitted][1] - face[0][1],
                                      corners[omitted][2] - face[0][2]};
        const double towardOmitted = (n[0] * w[0]) + (n[1] * w[1]) + (n[2] * w[2]);
        INFO("face " << triangle << " omits tet node " << omitted << ", n.w = " << towardOmitted);
        CHECK(towardOmitted < 0.0);
    }
}

TEST_CASE("MeshView_RefusesAnInteriorSelectionItCannotHonour",
          "[renderer][meshview][validation]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();

    // Nothing to inspect is a refusal, not an empty view.
    CHECK(errorCode(MeshView::tetrahedraOf(mesh, {})) == ErrorCode::InvalidArgument);

    // An element that is not a tetrahedron of this mesh is REFUSED rather than
    // skipped: a caller that asked for two and silently got one would have
    // been told something false about the mesh.
    REQUIRE_FALSE(mesh.tetrahedra().empty());
    REQUIRE_FALSE(mesh.triangles().empty());
    const std::array<ElementId, 2> withAFacet{mesh.tetrahedra().front().id,
                                              mesh.triangles().front().id};
    CHECK(errorCode(MeshView::tetrahedraOf(mesh, withAFacet)) == ErrorCode::NotFound);
    const std::array<ElementId, 1> absent{ElementId::fromValue(999999U)};
    CHECK(errorCode(MeshView::tetrahedraOf(mesh, absent)) == ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Determinism, and the invariant the adapter exists to protect
// ---------------------------------------------------------------------------

TEST_CASE("MeshView_IsDeterministicForTheSameMesh", "[renderer][meshview][determinism]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();

    const MeshView first = require(MeshView::volumeBoundaryOf(mesh));
    const MeshView second = require(MeshView::volumeBoundaryOf(mesh));

    // Byte-identical buffers, not merely equal counts. Nothing in the adapter
    // may depend on an unordered container or on iteration order.
    REQUIRE(first.vertexCount() == second.vertexCount());
    REQUIRE(first.triangleCount() == second.triangleCount());
    REQUIRE(first.edgeCount() == second.edgeCount());
    CHECK(std::ranges::equal(first.positions(), second.positions()));
    CHECK(std::ranges::equal(first.triangleIndices(), second.triangleIndices()));
    CHECK(std::ranges::equal(first.edgeIndices(), second.edgeIndices()));
    for (std::size_t vertex = 0U; vertex < first.vertexCount(); ++vertex) {
        CHECK(require(first.nodeOfVertex(vertex)) == require(second.nodeOfVertex(vertex)));
    }
}

TEST_CASE("MeshView_BuildingAndQueryingAViewDoesNotChangeTheMesh",
          "[renderer][meshview][readonly]") {
    // MANDATORY, and the reason the adapter is shaped the way it is. A
    // meshing::Mesh has no mutator at all -- addNode and friends are on
    // MeshBuilder -- so this fingerprint cannot fail without someone having
    // added one. That is the point: the invariant is structural, and this test
    // is what would notice the structure changing.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshFingerprint before{mesh};

    const MeshView boundary = require(MeshView::volumeBoundaryOf(mesh));
    const MeshView surface = require(MeshView::surfaceOf(mesh));
    const std::array<ElementId, 1> chosen{mesh.tetrahedra().front().id};
    const MeshView interior = require(MeshView::tetrahedraOf(mesh, chosen));

    for (std::size_t vertex = 0U; vertex < boundary.vertexCount(); ++vertex) {
        (void)boundary.nodeOfVertex(vertex);
    }
    for (std::size_t triangle = 0U; triangle < boundary.triangleCount(); ++triangle) {
        (void)boundary.elementOfTriangle(triangle);
        (void)boundary.trianglesOfElement(require(boundary.elementOfTriangle(triangle)));
    }
    (void)boundary.describes(mesh);
    (void)surface.positions();
    (void)interior.edgeIndices();

    CHECK(MeshFingerprint{mesh} == before);
}
