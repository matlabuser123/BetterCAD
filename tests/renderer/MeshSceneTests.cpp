// P16-VIZ-001: the mesh inspection session.
//
// HEADLESS. The panel that displays this is Qt; what it displays is here, and
// is assertable without a window.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/renderer/MeshScene.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using meshing::ElementId;
using meshing::GeometryMeshMap;
using meshing::NodeId;
using meshing::QualityMetric;
using meshing::VolumeMesh;
using renderer::ElementInspection;
using renderer::MeshQualityView;
using renderer::MeshScene;
using renderer::MeshVisualState;
using renderer::NodeInspection;

namespace {

struct Block {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};

    Block() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, 30_mm, 20_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] VolumeMesh mesh() {
        Result<VolumeMesh> result = meshing::volumeMeshFor(document, regenerator, feature, {});
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }

    [[nodiscard]] GeometryMeshMap map(const VolumeMesh& volume) {
        Result<GeometryMeshMap> result =
            meshing::geometryMeshMapFor(document, regenerator, feature, volume);
        if (!result.has_value()) {
            FAIL("mapping refused: " << result.error().message);
        }
        return *result;
    }

    /// A policy that CLASSIFIES SOMETHING. reportOnlyThresholds() sets no
    /// limits, so every element is Valid and an inspection that ignored the
    /// report and answered Valid would agree with it everywhere -- a mutation
    /// doing exactly that survived this test until the policy had a range.
    [[nodiscard]] static meshing::QualityThresholds thresholds() {
        meshing::QualityThresholds limits;
        limits.limits[meshing::QualityMetric::TetAspectRatio] =
            meshing::QualityThreshold{.warning = 1.05, .failure = 3.0};
        return limits;
    }

    [[nodiscard]] MeshQualityView quality(const VolumeMesh& volume) {
        Result<MeshQualityView> result =
            MeshQualityView::evaluate(volume.mesh(), thresholds());
        if (!result.has_value()) {
            FAIL("quality refused: " << result.error().message);
        }
        return std::move(*result);
    }

    [[nodiscard]] MeshScene scene() {
        VolumeMesh volume = mesh();
        GeometryMeshMap mapping = map(volume);
        MeshQualityView measured = quality(volume);
        Result<MeshScene> result =
            MeshScene::adopt(feature, std::move(volume), std::move(mapping), std::move(measured));
        if (!result.has_value()) {
            FAIL("scene refused: " << result.error().message);
        }
        return std::move(*result);
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

} // namespace

TEST_CASE("MeshScene_RefusesASetThatDoesNotDescribeOneMesh", "[renderer][meshscene][revision]") {
    // THE REASON THIS CLASS EXISTS. A mesh, its mapping and its quality report
    // are only meaningful together. Checking that at every call site works
    // until one call site forgets; refusing it once, here, means nothing
    // inside the scene has to re-check and nothing can assemble a mismatched
    // set in the first place.
    Block block;
    VolumeMesh first = block.mesh();
    GeometryMeshMap firstMap = block.map(first);
    MeshQualityView firstQuality = block.quality(first);

    block.setDepth(14_mm);
    VolumeMesh second = block.mesh();
    GeometryMeshMap secondMap = block.map(second);
    MeshQualityView secondQuality = block.quality(second);

    // A map from the previous generation.
    Result<MeshScene> mixedMap = MeshScene::adopt(block.feature, second, firstMap, secondQuality);
    REQUIRE_FALSE(mixedMap.has_value());
    CHECK(mixedMap.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(mixedMap.error().message, ContainsSubstring("the mapping describes mesh"));

    // A quality report from the previous generation: the dangerous one,
    // because ElementId values are reused and navigation would silently land
    // on a different element.
    Result<MeshScene> mixedQuality =
        MeshScene::adopt(block.feature, second, secondMap, firstQuality);
    REQUIRE_FALSE(mixedQuality.has_value());
    CHECK_THAT(mixedQuality.error().message,
               ContainsSubstring("the quality report describes mesh"));

    // A matched set is accepted.
    CHECK(MeshScene::adopt(block.feature, second, secondMap, secondQuality).has_value());
}

TEST_CASE("MeshScene_RefusesAMeshThatIsNotThisFeatures", "[renderer][meshscene][validation]") {
    Block block;
    VolumeMesh volume = block.mesh();
    GeometryMeshMap mapping = block.map(volume);
    MeshQualityView measured = block.quality(volume);

    const Result<MeshScene> wrongFeature = MeshScene::adopt(
        ObjectId::fromValue(block.feature.value() + 1U), std::move(volume), std::move(mapping),
        std::move(measured));
    REQUIRE_FALSE(wrongFeature.has_value());
    CHECK_THAT(wrongFeature.error().message, ContainsSubstring("was built from"));

    Block other;
    VolumeMesh volume2 = other.mesh();
    GeometryMeshMap mapping2 = other.map(volume2);
    MeshQualityView measured2 = other.quality(volume2);
    CHECK(errorCode(MeshScene::adopt(ObjectId{}, std::move(volume2), std::move(mapping2),
                                     std::move(measured2))) == ErrorCode::InvalidArgument);
}

TEST_CASE("MeshScene_InspectsANodeAgainstTheCanonicalMesh", "[renderer][meshscene][inspection]") {
    Block block;
    const MeshScene scene = block.scene();
    const meshing::Mesh& mesh = scene.mesh();
    REQUIRE_FALSE(mesh.triangles().empty());

    // A boundary node: it is on the boundary, its position is the canonical
    // one exactly, and it reports the CAD face behind one of its facets.
    const NodeId boundaryNode = mesh.triangles().front().nodes[0];
    const Result<NodeInspection> boundary = scene.inspectNode(boundaryNode);
    REQUIRE(boundary.has_value());
    CHECK(boundary->node == boundaryNode);
    CHECK(boundary->onBoundary);
    const meshing::Node* canonical = mesh.findNode(boundaryNode);
    REQUIRE(canonical != nullptr);
    // EXACT equality: the inspection reports the mesh's own Point3D, with its
    // unit, and not a value recovered from a render buffer.
    CHECK(boundary->position == canonical->position);
    REQUIRE(boundary->source.has_value());
    CHECK(boundary->source->face < scene.map().faces().size());

    // A node that is in no mesh.
    CHECK(errorCode(scene.inspectNode(NodeId::fromValue(999999U))) == ErrorCode::NotFound);
}

TEST_CASE("MeshScene_InspectsATetrahedronWithTheReportsOwnNumbers",
          "[renderer][meshscene][inspection]") {
    Block block;
    const MeshScene scene = block.scene();
    const meshing::Mesh& mesh = scene.mesh();
    REQUIRE_FALSE(mesh.tetrahedra().empty());

    const meshing::Tetrahedron& tet = mesh.tetrahedra().front();
    const Result<ElementInspection> inspection = scene.inspectElement(tet.id);
    REQUIRE(inspection.has_value());
    CHECK(inspection->element == tet.id);
    CHECK(inspection->type == meshing::ElementType::Tetrahedron4);
    REQUIRE(inspection->nodes.size() == 4U);
    CHECK(std::ranges::equal(inspection->nodes, tet.nodes));

    // THE SIGN IS KEPT. A valid Tet4 is positively oriented and the inspection
    // reports the signed value, because the sign is the only evidence of an
    // inverted element.
    REQUIRE(inspection->signedVolume.has_value());
    CHECK(inspection->signedVolume->si() > 0.0);

    // And the volume is core's own, not recomputed here.
    const meshing::Node* p1 = mesh.findNode(tet.nodes[0]);
    const meshing::Node* p2 = mesh.findNode(tet.nodes[1]);
    const meshing::Node* p3 = mesh.findNode(tet.nodes[2]);
    const meshing::Node* p4 = mesh.findNode(tet.nodes[3]);
    REQUIRE(p1 != nullptr);
    CHECK(inspection->signedVolume->si() ==
          meshing::signedVolume(p1->position, p2->position, p3->position, p4->position).si());

    // The quality numbers are the report's entry for this element, field for
    // field -- no separate calculation in the inspection layer.
    REQUIRE(inspection->tetQuality.has_value());
    const auto reported = std::ranges::find_if(
        scene.quality().report().tets,
        [&tet](const meshing::TetQuality& measured) { return measured.element == tet.id; });
    REQUIRE(reported != scene.quality().report().tets.end());
    CHECK(*inspection->tetQuality == *reported);

    // A tetrahedron is not a boundary facet, so it has no CAD face.
    CHECK_FALSE(inspection->source.has_value());
}

TEST_CASE("MeshScene_InspectsABoundaryFacetAndNamesItsCadFace",
          "[renderer][meshscene][inspection]") {
    Block block;
    const MeshScene scene = block.scene();
    const meshing::Mesh& mesh = scene.mesh();
    REQUIRE_FALSE(mesh.triangles().empty());

    const meshing::Triangle& facet = mesh.triangles().front();
    const Result<ElementInspection> inspection = scene.inspectElement(facet.id);
    REQUIRE(inspection.has_value());
    CHECK(inspection->type == meshing::ElementType::Triangle3);
    REQUIRE(inspection->nodes.size() == 3U);
    CHECK(std::ranges::equal(inspection->nodes, facet.nodes));
    // A triangle has no signed volume, and the field is absent rather than 0.
    CHECK_FALSE(inspection->signedVolume.has_value());
    REQUIRE(inspection->source.has_value());
    CHECK(inspection->source->facet == facet.id);

    CHECK(errorCode(scene.inspectElement(ElementId::fromValue(999999U))) == ErrorCode::NotFound);
}

TEST_CASE("MeshScene_ClassifiesEveryElementAsTheReportDoes",
          "[renderer][meshscene][inspection]") {
    Block block;
    const MeshScene scene = block.scene();
    const meshing::Mesh& mesh = scene.mesh();

    // The report must have classified something, or an inspection that
    // answered Valid unconditionally would pass every assertion below.
    const meshing::MeshQualityReport& report = scene.quality().report();
    INFO("valid " << report.validElements << ", warning " << report.warningElements
                  << ", failure " << report.failureElements);
    REQUIRE(report.warningElements + report.failureElements > 0U);

    // The GUI holds no thresholds: every classification must be the report's.
    std::size_t nonValid = 0U;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const Result<renderer::ElementInspection> inspection = scene.inspectElement(tet.id);
        REQUIRE(inspection.has_value());
        const Result<meshing::QualityClass> fromReport = scene.quality().classOf(mesh, tet.id);
        REQUIRE(fromReport.has_value());
        INFO("element " << tet.id);
        CHECK(inspection->classification == *fromReport);
        if (inspection->classification != meshing::QualityClass::Valid) {
            ++nonValid;
        }
    }
    CHECK(nonValid > 0U);
}

TEST_CASE("MeshScene_NavigatesToTheWorstElementForEachMetric",
          "[renderer][meshscene][quality]") {
    Block block;
    const MeshScene scene = block.scene();
    const std::array<QualityMetric, 4> metrics{QualityMetric::TetAspectRatio,
                                               QualityMetric::TetRadiusRatio,
                                               QualityMetric::TetMinDihedralAngle,
                                               QualityMetric::TetMaxDihedralAngle};
    for (const QualityMetric metric : metrics) {
        const Result<ElementId> worst = scene.worstElementFor(metric);
        INFO("metric " << meshing::toString(metric));
        REQUIRE(worst.has_value());
        // It is the report's answer, and it is an element that can be
        // inspected -- which is what "navigate to it" needs.
        const auto summary = scene.quality().report().summaries.find(metric);
        REQUIRE(summary != scene.quality().report().summaries.end());
        CHECK(*worst == summary->second.worst);
        CHECK(scene.inspectElement(*worst).has_value());
    }
}

TEST_CASE("MeshScene_ListsEveryCadFaceIncludingUnnamedOnes", "[renderer][meshscene][linkage]") {
    // A panel lists faces so a user can pick one. An unnamed face must be
    // listed and must be selectable, because a drilled hole's wall has no
    // FaceName and is otherwise unreachable.
    Block block;
    const MeshScene scene = block.scene();
    const std::vector<MeshScene::FaceEntry> faces = scene.faces();

    REQUIRE(faces.size() == scene.map().faces().size());
    std::size_t facets = 0U;
    for (std::size_t index = 0U; index < faces.size(); ++index) {
        CHECK(faces[index].index == index);
        CHECK_FALSE(faces[index].label.empty());
        CHECK(faces[index].facetCount == scene.map().faces()[index].facets.size());
        facets += faces[index].facetCount;
    }
    // Every boundary facet belongs to exactly one listed face.
    CHECK(facets == scene.mesh().triangles().size());

    // A block's faces are all named by the extrude.
    for (const MeshScene::FaceEntry& face : faces) {
        CHECK(face.named);
    }
}

TEST_CASE("MeshScene_ReportsItsStateFromTheDocument", "[renderer][meshscene][meshstate]") {
    Block block;
    const MeshScene scene = block.scene();
    CHECK(scene.status(block.document).state == MeshVisualState::Current);

    block.setDepth(14_mm);
    const renderer::MeshStatus stale = scene.status(block.document);
    CHECK(stale.state == MeshVisualState::Stale);
    CHECK(stale.inspectable);

    // A failed attempt on top of a stale mesh: GenerationFailed, and the old
    // mesh is still there to inspect.
    const renderer::MeshStatus failed =
        scene.status(block.document, Error{ErrorCode::Internal, "the backend refused"});
    CHECK(failed.state == MeshVisualState::GenerationFailed);
    CHECK(failed.inspectable);
}
