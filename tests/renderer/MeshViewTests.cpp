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
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <span>
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
using Catch::Matchers::ContainsSubstring;

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

// ---------------------------------------------------------------------------
// The boundary derived from the tetrahedra (ADR-046)
// ---------------------------------------------------------------------------

namespace {

/// The sorted node triple of render triangle @p triangle, read through the
/// index bridge rather than by arithmetic on the buffers.
[[nodiscard]] std::array<NodeId, 3> tripleOfTriangle(const MeshView& view,
                                                     std::size_t triangle) {
    const std::span<const std::uint32_t> indices = view.triangleIndices();
    std::array<NodeId, 3> nodes{};
    for (std::size_t corner = 0; corner < 3U; ++corner) {
        const Result<NodeId> node = view.nodeOfVertex(indices[(triangle * 3U) + corner]);
        REQUIRE(node.has_value());
        nodes[corner] = *node;
    }
    std::ranges::sort(nodes);
    return nodes;
}

/// Every sorted node triple a view draws.
[[nodiscard]] std::set<std::array<NodeId, 3>> trianglesAsTriples(const MeshView& view) {
    std::set<std::array<NodeId, 3>> triples;
    for (std::size_t triangle = 0; triangle < view.triangleCount(); ++triangle) {
        triples.insert(tripleOfTriangle(view, triangle));
    }
    return triples;
}

} // namespace

TEST_CASE("MeshView_ATetrahedronDerivedBoundaryIsTheSameSurfaceAsTheTriangle3Boundary",
          "[renderer][meshview][interior]") {
    // THE VALIDATION ADR-046 RESTS ON, and it is an INDEPENDENT one: the two
    // boundaries are built from different data by different code -- one from
    // the mesh's Triangle3 elements, one by counting face incidences over the
    // Tet4 elements -- so comparing them checks each against something other
    // than the code that produced it.
    //
    // If they disagree, either the premise of ADR-046 is wrong or the mesh is,
    // and both are worth knowing.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    REQUIRE_FALSE(mesh.tetrahedra().empty());
    REQUIRE_FALSE(mesh.triangles().empty());

    const MeshView byTriangle = require(MeshView::volumeBoundaryOf(mesh));
    const MeshView byTetrahedron = require(MeshView::volumeBoundaryByTetrahedronOf(mesh));

    // NON-VACUITY FIRST: both drew something, and the counts match. Comparing
    // two empty sets would pass and say nothing.
    INFO("by triangle " << byTriangle.triangleCount() << ", by tetrahedron "
                        << byTetrahedron.triangleCount());
    REQUIRE(byTriangle.triangleCount() > 0U);
    REQUIRE(byTetrahedron.triangleCount() == byTriangle.triangleCount());

    const std::set<std::array<NodeId, 3>> fromTriangles = trianglesAsTriples(byTriangle);
    const std::set<std::array<NodeId, 3>> fromTetrahedra = trianglesAsTriples(byTetrahedron);

    // NO DUPLICATE FACE in either, which the set would otherwise hide.
    REQUIRE(fromTriangles.size() == byTriangle.triangleCount());
    REQUIRE(fromTetrahedra.size() == byTetrahedron.triangleCount());

    // THE SAME SURFACE, compared BOTH WAYS. One direction alone would accept a
    // strict subset.
    CHECK(fromTetrahedra == fromTriangles);
    for (const std::array<NodeId, 3>& face : fromTriangles) {
        INFO("the Triangle3 face on " << face[0].value() << ", " << face[1].value() << ", "
                                      << face[2].value() << " is missing from the tet-derived view");
        REQUIRE(fromTetrahedra.contains(face));
    }
    for (const std::array<NodeId, 3>& face : fromTetrahedra) {
        INFO("the tet-derived face on " << face[0].value() << ", " << face[1].value() << ", "
                                        << face[2].value() << " is not a Triangle3 of the mesh");
        REQUIRE(fromTriangles.contains(face));
    }

    // AND THE SAME NODES ARE DRAWN. A boundary that covered the same faces but
    // referenced different nodes would be impossible, so this is cheap, but it
    // is the buffer a caller actually reads.
    CHECK(byTetrahedron.vertexCount() == byTriangle.vertexCount());
}

TEST_CASE("MeshView_ATetrahedronDerivedBoundaryNamesTetrahedraNotTriangles",
          "[renderer][meshview][interior]") {
    // THE WHOLE POINT OF THE VIEW. `volumeBoundaryOf` names Triangle3
    // elements, whose ids cannot index a per-TETRAHEDRON field; this one names
    // the tetrahedron that owns each drawn face.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();

    const MeshView view = require(MeshView::volumeBoundaryByTetrahedronOf(mesh));
    CHECK(view.source() == MeshSource::VolumeBoundaryByTetrahedron);
    REQUIRE(view.triangleCount() > 0U);

    std::set<ElementId> named;
    for (std::size_t triangle = 0; triangle < view.triangleCount(); ++triangle) {
        const Result<ElementId> element = view.elementOfTriangle(triangle);
        REQUIRE(element.has_value());
        INFO("render triangle " << triangle << " names element " << element->value());
        // EVERY NAMED ELEMENT IS A TETRAHEDRON OF THIS MESH, asked of the mesh
        // rather than inferred from the id.
        REQUIRE(mesh.findTetrahedron(*element) != nullptr);
        named.insert(*element);
    }

    // AND THE DISCRIMINATOR AGAINST THE OTHER BOUNDARY: the Triangle3 view
    // names things that are NOT tetrahedra. Without this the test above would
    // pass on a mesh whose triangle and tetrahedron ids happened to overlap.
    const MeshView byTriangle = require(MeshView::volumeBoundaryOf(mesh));
    std::size_t namedTriangles = 0;
    for (std::size_t triangle = 0; triangle < byTriangle.triangleCount(); ++triangle) {
        const Result<ElementId> element = byTriangle.elementOfTriangle(triangle);
        REQUIRE(element.has_value());
        if (mesh.findTetrahedron(*element) == nullptr) {
            ++namedTriangles;
        }
    }
    INFO(namedTriangles << " of " << byTriangle.triangleCount()
                        << " Triangle3-view elements are not tetrahedra");
    REQUIRE(namedTriangles > 0U);

    // A BOUNDARY TETRAHEDRON MAY OWN MORE THAN ONE EXTERIOR FACE -- a corner
    // tetrahedron of a block can own three -- so there are FEWER named
    // tetrahedra than drawn triangles, and trianglesOfElement groups them.
    INFO(named.size() << " tetrahedra own " << view.triangleCount() << " exterior faces");
    CHECK(named.size() <= view.triangleCount());
    std::size_t grouped = 0;
    for (const ElementId tet : named) {
        const std::size_t faces = view.trianglesOfElement(tet).size();
        REQUIRE(faces >= 1U);
        REQUIRE(faces <= 4U);
        grouped += faces;
    }
    // THE GROUPING ACCOUNTS FOR EVERY DRAWN TRIANGLE AND NO OTHER.
    CHECK(grouped == view.triangleCount());
}

TEST_CASE("MeshView_ATetrahedronDerivedBoundaryDrawsNoInteriorFace",
          "[renderer][meshview][interior]") {
    // EXTERIOR MEANS CARRIED BY EXACTLY ONE TETRAHEDRON. Counted here from the
    // mesh directly, so the view is checked against the definition rather than
    // against its own implementation.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();

    // Every tetrahedron face in the mesh, with how many tetrahedra carry it.
    std::map<std::array<NodeId, 3>, std::size_t> carriers;
    constexpr std::array<std::array<std::size_t, 3>, 4> faces{
        {{{0U, 2U, 1U}}, {{0U, 1U, 3U}}, {{1U, 2U, 3U}}, {{0U, 3U, 2U}}}};
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        for (const std::array<std::size_t, 3>& face : faces) {
            std::array<NodeId, 3> key{tet.nodes[face[0]], tet.nodes[face[1]], tet.nodes[face[2]]};
            std::ranges::sort(key);
            ++carriers[key];
        }
    }

    // NON-VACUITY: this mesh really has interior faces, otherwise "draws no
    // interior face" is satisfied by a mesh that has none to draw.
    const std::size_t interior =
        static_cast<std::size_t>(std::ranges::count_if(carriers, [](const auto& entry) {
            return entry.second == 2U;
        }));
    const std::size_t exterior =
        static_cast<std::size_t>(std::ranges::count_if(carriers, [](const auto& entry) {
            return entry.second == 1U;
        }));
    INFO(exterior << " exterior faces, " << interior << " interior");
    REQUIRE(interior > 0U);
    REQUIRE(exterior > 0U);
    // AND NO FACE IS CARRIED BY THREE: this mesh is manifold, so the refusal
    // path below is genuinely unreachable here rather than merely untaken.
    for (const auto& [key, count] : carriers) {
        REQUIRE(count <= 2U);
    }

    const MeshView view = require(MeshView::volumeBoundaryByTetrahedronOf(mesh));

    // EVERY DRAWN FACE IS EXTERIOR, and the count is exactly the exterior
    // count -- so no interior face slipped in and none was dropped.
    CHECK(view.triangleCount() == exterior);
    for (std::size_t triangle = 0; triangle < view.triangleCount(); ++triangle) {
        const std::array<NodeId, 3> triple = tripleOfTriangle(view, triangle);
        const auto found = carriers.find(triple);
        REQUIRE(found != carriers.end());
        INFO("render triangle " << triangle << " is carried by " << found->second
                                << " tetrahedra");
        REQUIRE(found->second == 1U);
    }
}

TEST_CASE("MeshView_ATetrahedronDerivedBoundaryFacesOutOfTheSolid",
          "[renderer][meshview][interior]") {
    // THE WINDING, CHECKED ANALYTICALLY AND NOT AGAINST kTetFaces. A normal
    // that pointed inward would light and cull wrongly, and a contour would be
    // painted on faces turned away from the camera.
    //
    // THE BLOCK IS CONVEX, so for every boundary face the outward normal has a
    // positive component along (face centroid - body centre). That is a
    // property of the solid, independent of the face table and of P16's own
    // Triangle3 orientation.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshView view = require(MeshView::volumeBoundaryByTetrahedronOf(mesh));
    REQUIRE(view.triangleCount() > 0U);

    // The body centre, from the canonical nodes rather than the render buffer.
    double cx = 0.0;
    double cy = 0.0;
    double cz = 0.0;
    for (const meshing::Node& node : mesh.nodes()) {
        cx += node.position.x.si();
        cy += node.position.y.si();
        cz += node.position.z.si();
    }
    const auto count = static_cast<double>(mesh.nodes().size());
    REQUIRE(count > 0.0);
    cx /= count;
    cy /= count;
    cz /= count;

    const std::span<const double> positions = view.positions();
    const std::span<const std::uint32_t> indices = view.triangleIndices();
    std::size_t outward = 0;
    for (std::size_t triangle = 0; triangle < view.triangleCount(); ++triangle) {
        const std::size_t a = indices[(triangle * 3U) + 0U] * 3U;
        const std::size_t b = indices[(triangle * 3U) + 1U] * 3U;
        const std::size_t c = indices[(triangle * 3U) + 2U] * 3U;
        const std::array<double, 3> u{positions[b] - positions[a], positions[b + 1U] - positions[a + 1U],
                                      positions[b + 2U] - positions[a + 2U]};
        const std::array<double, 3> v{positions[c] - positions[a], positions[c + 1U] - positions[a + 1U],
                                      positions[c + 2U] - positions[a + 2U]};
        const std::array<double, 3> n{(u[1] * v[2]) - (u[2] * v[1]), (u[2] * v[0]) - (u[0] * v[2]),
                                      (u[0] * v[1]) - (u[1] * v[0])};
        // THE FACE IS NOT DEGENERATE, which a zero normal would make the dot
        // product below pass on.
        const double area = std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
        INFO("render triangle " << triangle << " has |n| = " << area);
        REQUIRE(area > 0.0);

        const std::array<double, 3> mid{
            ((positions[a] + positions[b] + positions[c]) / 3.0) - cx,
            ((positions[a + 1U] + positions[b + 1U] + positions[c + 1U]) / 3.0) - cy,
            ((positions[a + 2U] + positions[b + 2U] + positions[c + 2U]) / 3.0) - cz};
        const double dot = (n[0] * mid[0]) + (n[1] * mid[1]) + (n[2] * mid[2]);
        INFO("render triangle " << triangle << " outwardness " << dot);
        REQUIRE(dot > 0.0);
        ++outward;
    }
    CHECK(outward == view.triangleCount());
}

TEST_CASE("MeshView_ATetrahedronDerivedBoundaryIsDeterministicAndAscending",
          "[renderer][meshview][interior]") {
    // DETERMINISM IS A PROMISE THIS CLASS MAKES: the same mesh gives
    // byte-identical buffers. The face incidences are collected into a vector
    // and SORTED rather than hashed, precisely so that holds -- an
    // unordered_map would make the facet order depend on the standard
    // library's bucketing.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();

    const MeshView first = require(MeshView::volumeBoundaryByTetrahedronOf(mesh));
    const MeshView second = require(MeshView::volumeBoundaryByTetrahedronOf(mesh));

    REQUIRE(first.triangleCount() > 0U);
    REQUIRE(first.triangleCount() == second.triangleCount());
    REQUIRE(first.vertexCount() == second.vertexCount());
    CHECK(std::ranges::equal(first.positions(), second.positions()));
    CHECK(std::ranges::equal(first.triangleIndices(), second.triangleIndices()));
    CHECK(std::ranges::equal(first.edgeIndices(), second.edgeIndices()));

    // ASCENDING ElementId, which is what this class documents its triangles to
    // be -- and `build` sorts the vertices and the edges but NOT the facets,
    // so it is this factory that has to deliver it.
    ElementId previous{};
    bool started = false;
    for (std::size_t triangle = 0; triangle < first.triangleCount(); ++triangle) {
        const Result<ElementId> element = first.elementOfTriangle(triangle);
        REQUIRE(element.has_value());
        if (started) {
            INFO("triangle " << triangle << " names " << element->value() << " after "
                             << previous.value());
            REQUIRE(previous <= *element);
        }
        previous = *element;
        started = true;
    }
    // NON-VACUITY: the ids really do advance, so the check above is not
    // satisfied by one element owning everything.
    REQUIRE(started);
    const Result<ElementId> firstElement = first.elementOfTriangle(0U);
    REQUIRE(firstElement.has_value());
    INFO("first " << firstElement->value() << ", last " << previous.value());
    REQUIRE(previous > *firstElement);
}

TEST_CASE("MeshView_ATetrahedronDerivedBoundaryRefusesAMeshWithNoTetrahedron",
          "[renderer][meshview][interior]") {
    // AN ENGINEERING SURFACE HAS NO TETRAHEDRON TO NAME, so this factory
    // refuses rather than returning an empty view -- and rather than returning
    // a view whose element ids are silently Triangle3s, which is the exact
    // confusion the new MeshSource exists to prevent.
    Block block;
    Result<meshing::EngineeringSurfaceMesh> surface =
        meshing::surfaceMeshFor(block.document, block.regenerator, block.feature, {});
    INFO((surface.has_value() ? std::string{} : surface.error().message));
    REQUIRE(surface.has_value());
    const Mesh& mesh = surface->mesh;

    // THE PRECONDITION: this really is a mesh with triangles and no tetrahedra,
    // so the refusal is about the absence of tetrahedra and nothing else.
    REQUIRE_FALSE(mesh.triangles().empty());
    REQUIRE(mesh.tetrahedra().empty());

    // AND THE OTHER TWO FACTORIES STILL WORK ON IT, so the refusal is specific.
    REQUIRE(MeshView::surfaceOf(mesh).has_value());

    const Result<MeshView> refused = MeshView::volumeBoundaryByTetrahedronOf(mesh);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("no tetrahedron"));
}

TEST_CASE("MeshView_ATetrahedronDerivedBoundaryRefusesANonManifoldFace",
          "[renderer][meshview][interior]") {
    // THE GAP PROBE E7 FOUND. Deleting the `shared > 2U` refusal survived every
    // one of the 88 cases, because no mesh the mesher produces has a face
    // carried by three tetrahedra -- and the other new case asserts, as a
    // precondition, that this fixture has none.
    //
    // I WAS ABOUT TO RECORD IT AS UNREACHABLE. It is not: `MeshBuilder` is
    // public and `addTetrahedron` checks CONNECTIVITY ONLY -- whether the four
    // points enclose a positive volume is a whole-mesh question that
    // validation answers later, by its own documented decision. So a
    // non-manifold mesh can be built by hand, and the guard can be reached
    // through the public API after all.
    //
    // WHAT IT PREVENTS: with the refusal gone, a face carried by three
    // tetrahedra is counted as interior (`shared == 1U` is false) and silently
    // DROPPED, so the boundary would have a hole in it and nothing would say
    // so. Treating it as exterior would be worse -- three overlapping facets
    // drawn with contradictory normals.
    meshing::MeshBuilder builder;
    const Result<meshing::NodeId> n0 =
        builder.addNode(Point3D{0_mm, 0_mm, 0_mm});
    const Result<meshing::NodeId> n1 =
        builder.addNode(Point3D{10_mm, 0_mm, 0_mm});
    const Result<meshing::NodeId> n2 =
        builder.addNode(Point3D{0_mm, 10_mm, 0_mm});
    // THREE DISTINCT APEXES, so three tetrahedra share the face {n0, n1, n2}.
    const Result<meshing::NodeId> above =
        builder.addNode(Point3D{0_mm, 0_mm, 10_mm});
    const Result<meshing::NodeId> below =
        builder.addNode(Point3D{0_mm, 0_mm, -10_mm});
    const Result<meshing::NodeId> aside =
        builder.addNode(Point3D{10_mm, 10_mm, 10_mm});
    REQUIRE(n0.has_value());
    REQUIRE(n1.has_value());
    REQUIRE(n2.has_value());
    REQUIRE(above.has_value());
    REQUIRE(below.has_value());
    REQUIRE(aside.has_value());

    const meshing::RegionId region = meshing::RegionId::fromValue(1);
    for (const Result<meshing::NodeId>& apex : {above, below, aside}) {
        const Result<ElementId> tet =
            builder.addTetrahedron({*n0, *n1, *n2, *apex}, region);
        INFO((tet.has_value() ? std::string{} : tet.error().message));
        REQUIRE(tet.has_value());
    }
    const Mesh mesh = builder.build();
    REQUIRE(mesh.tetrahedra().size() == 3U);

    const Result<MeshView> refused = MeshView::volumeBoundaryByTetrahedronOf(mesh);
    INFO((refused.has_value() ? std::string{"built a view it should have refused"}
                              : refused.error().message));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("not manifold"));
    // THE COUNT IS NAMED, so the diagnostic says how bad it is and not merely
    // that something is wrong.
    CHECK_THAT(refused.error().message, ContainsSubstring("3 tetrahedra"));

    SECTION("and TWO tetrahedra sharing a face are fine, so the refusal is specific") {
        // THE DISCRIMINATOR. Without this the refusal above would be satisfied
        // by a factory that rejected any shared face at all -- which would
        // reject every real volume mesh, since an interior face is exactly a
        // face shared by two.
        meshing::MeshBuilder pair;
        const Result<meshing::NodeId> p0 = pair.addNode(Point3D{0_mm, 0_mm, 0_mm});
        const Result<meshing::NodeId> p1 = pair.addNode(Point3D{10_mm, 0_mm, 0_mm});
        const Result<meshing::NodeId> p2 = pair.addNode(Point3D{0_mm, 10_mm, 0_mm});
        const Result<meshing::NodeId> p3 = pair.addNode(Point3D{0_mm, 0_mm, 10_mm});
        const Result<meshing::NodeId> p4 = pair.addNode(Point3D{0_mm, 0_mm, -10_mm});
        REQUIRE(p0.has_value());
        REQUIRE(p1.has_value());
        REQUIRE(p2.has_value());
        REQUIRE(p3.has_value());
        REQUIRE(p4.has_value());
        REQUIRE(pair.addTetrahedron({*p0, *p1, *p2, *p3}, region).has_value());
        REQUIRE(pair.addTetrahedron({*p0, *p1, *p2, *p4}, region).has_value());
        const Mesh twoTets = pair.build();

        const Result<MeshView> built = MeshView::volumeBoundaryByTetrahedronOf(twoTets);
        INFO((built.has_value() ? std::string{} : built.error().message));
        REQUIRE(built.has_value());
        // THE SHARED FACE IS INTERIOR AND NOT DRAWN: two tetrahedra have eight
        // faces, of which the shared one is carried twice, so SIX are drawn.
        CHECK(built->triangleCount() == 6U);
    }
}
