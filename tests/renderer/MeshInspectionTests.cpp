// P16-VIZ-001: quality inspection, boundary highlighting and current/stale
// state.
//
// HEADLESS. No view, no graphic driver, no GL.
//
// THE CENTRAL HAZARD THESE TESTS GUARD. NodeId and ElementId are reused across
// mesh generations (ADR-031). A quality report, a mapping or a selection that
// outlives a remesh can match a NUMBER against a different physical element
// and look entirely successful doing it. Most of what follows is therefore not
// "does the right answer come back" but "is the wrong answer REFUSED".

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
#include <bettercad/renderer/MeshInspection.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using meshing::ElementId;
using meshing::GeometryMeshMap;
using meshing::Mesh;
using meshing::QualityMetric;
using meshing::VolumeMesh;
using renderer::MeshHighlight;
using renderer::MeshHolding;
using renderer::MeshQualityView;
using renderer::MeshStatus;
using renderer::MeshView;
using renderer::MeshVisualState;

namespace {

struct Block {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    std::array<EntityId, 4> lines{};

    explicit Block(Length a = 30_mm, Length b = 20_mm, Length c = 10_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, a, b);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName top() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines[which]}};
    }

    [[nodiscard]] VolumeMesh mesh(const meshing::VolumeMeshControls& controls = {}) {
        Result<VolumeMesh> result = meshing::volumeMeshFor(document, regenerator, feature, controls);
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

/// A block with a drilled through hole: the reference case for the forward
/// linkage, because the hole's cylindrical wall has NO FaceName.
struct Bored {
    Document document{"Bored"};
    features::Regenerator regenerator;
    ObjectId solid{};
    ObjectId bore{};

    Bored() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
        REQUIRE(extrude.has_value());
        solid = require(document.addObject(std::move(*extrude)));
        const geometry::FaceSignature startPlane =
            geometry::planeSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitZ().reversed());
        auto hole = features::HoleFeature::create(
            "Bore", {.target = FeatureId::fromValue(solid.value()),
                     .face = startPlane,
                     .center = Point2D{20_mm, 15_mm},
                     .extent = geometry::HoleExtent::Through,
                     .diameter = 12_mm});
        if (!hole.has_value()) {
            FAIL("hole refused: " << hole.error().message);
        }
        bore = require(document.addObject(std::move(*hole)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] VolumeMesh mesh(const meshing::VolumeMeshControls& controls = {}) {
        Result<VolumeMesh> result =
            meshing::volumeMeshFor(document, regenerator, bore, controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }

    [[nodiscard]] GeometryMeshMap map(const VolumeMesh& volume) {
        Result<GeometryMeshMap> result =
            meshing::geometryMeshMapFor(document, regenerator, bore, volume);
        if (!result.has_value()) {
            FAIL("mapping refused: " << result.error().message);
        }
        return *result;
    }
};

/// The bore's cylindrical wall: the one mapped face with a cylinder and no
/// name. P16-MAP-001: cutHole names a hole's flat faces and not its wall.
[[nodiscard]] std::size_t wallFaceOf(const GeometryMeshMap& map) {
    for (const meshing::MappedFace& face : map.faces()) {
        if (face.cylinder.has_value() && face.names.empty()) {
            return face.index;
        }
    }
    FAIL("no unnamed cylindrical face: the bore wall was not found");
    return 0U;
}

/// A policy that CLASSIFIES SOMETHING.
///
/// WHY THIS HAD TO EXIST. `reportOnlyThresholds()` sets no limits, so every
/// element comes back Valid and a `classOf` that ignored the report entirely
/// and answered Valid would agree with it on every element of every fixture.
/// A mutation doing exactly that SURVIVED both classification tests. A test
/// that cannot tell the right answer from a constant is not testing anything.
///
/// The bounds are deliberately tight rather than realistic: the point is to
/// produce Warning and Failure findings on an ordinary mesh so the
/// classification has a range to be read out of. The thresholds are the
/// report's, never the GUI's -- which is the property under test.
[[nodiscard]] meshing::QualityThresholds classifyingThresholds() {
    meshing::QualityThresholds thresholds;
    thresholds.limits[meshing::QualityMetric::TetAspectRatio] =
        meshing::QualityThreshold{.warning = 1.05, .failure = 3.0};
    return thresholds;
}

[[nodiscard]] MeshView requireView(Result<MeshView> view) {
    if (!view.has_value()) {
        FAIL("the mesh view was refused: " << view.error().message);
    }
    return std::move(*view);
}

[[nodiscard]] MeshQualityView requireQuality(Result<MeshQualityView> view) {
    if (!view.has_value()) {
        FAIL("the quality view was refused: " << view.error().message);
    }
    return std::move(*view);
}

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

} // namespace

// ---------------------------------------------------------------------------
// Current, stale, missing, failed
// ---------------------------------------------------------------------------

TEST_CASE("MeshStatus_ReportsNoMeshBeforeAnythingIsGenerated", "[renderer][meshstate]") {
    Block block;
    const MeshStatus status = renderer::statusOf(block.document, MeshHolding{});
    CHECK(status.state == MeshVisualState::NoMesh);
    CHECK_FALSE(status.inspectable);
    CHECK_FALSE(status.failure.has_value());
    // An empty scene plus a green "Current" is the thing this prevents.
    CHECK(renderer::toString(status.state) == "no engineering mesh");
}

TEST_CASE("MeshStatus_ReportsCurrentForAMeshOfTheGeometryTheDocumentHasNow",
          "[renderer][meshstate]") {
    Block block;
    MeshHolding holding;
    holding.mesh = block.mesh();
    const MeshStatus status = renderer::statusOf(block.document, holding);
    CHECK(status.state == MeshVisualState::Current);
    CHECK(status.inspectable);
}

TEST_CASE("MeshStatus_BecomesStaleWhenTheGeometryChanges", "[renderer][meshstate]") {
    // THE SEQUENCE THE MILESTONE IS JUDGED ON: M1 current, edit, M1 stale,
    // generate M2, current again. Driven entirely by the document's own
    // geometry revision -- there is no GUI timestamp anywhere in this path.
    Block block;
    MeshHolding holding;
    holding.mesh = block.mesh();
    REQUIRE(renderer::statusOf(block.document, holding).state == MeshVisualState::Current);

    block.setDepth(14_mm);
    const MeshStatus stale = renderer::statusOf(block.document, holding);
    CHECK(stale.state == MeshVisualState::Stale);
    // STILL INSPECTABLE. A stale mesh is old, not gone.
    CHECK(stale.inspectable);

    holding.mesh = block.mesh();
    CHECK(renderer::statusOf(block.document, holding).state == MeshVisualState::Current);
}

TEST_CASE("MeshStatus_AFailedRemeshCannotLookCurrent", "[renderer][meshstate]") {
    // An old mesh surviving a failed attempt is the case where "stale" and
    // "current" are both wrong answers. It must read as GenerationFailED, and
    // the old mesh must remain inspectable and be labelled as what it is.
    Block block;
    MeshHolding holding;
    holding.mesh = block.mesh();
    REQUIRE(renderer::statusOf(block.document, holding).state == MeshVisualState::Current);

    holding.lastFailure = Error{ErrorCode::FailedPrecondition, "the backend refused the boundary"};
    const MeshStatus failed = renderer::statusOf(block.document, holding);
    CHECK(failed.state == MeshVisualState::GenerationFailed);
    CHECK(failed.inspectable);
    REQUIRE(failed.failure.has_value());
    CHECK_THAT(failed.failure->message, ContainsSubstring("refused the boundary"));

    // And a failure with no mesh at all is still GenerationFailed, not NoMesh:
    // "nothing was ever generated" and "the attempt failed" are different
    // things to tell a user.
    MeshHolding never;
    never.lastFailure = Error{ErrorCode::Internal, "no backend"};
    const MeshStatus none = renderer::statusOf(block.document, never);
    CHECK(none.state == MeshVisualState::GenerationFailed);
    CHECK_FALSE(none.inspectable);
}

// ---------------------------------------------------------------------------
// Quality
// ---------------------------------------------------------------------------

TEST_CASE("MeshQualityView_NavigatesToTheReportsOwnWorstElementPerMetric",
          "[renderer][meshquality]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const MeshQualityView quality =
        requireQuality(MeshQualityView::evaluate(mesh, meshing::reportOnlyThresholds()));

    // PER METRIC, because P16-QUALITY-001 reports worst per metric and
    // deliberately has no overall score. The GUI's answer must be the
    // report's, so it is compared against the report directly.
    const std::array<QualityMetric, 4> metrics{QualityMetric::TetAspectRatio,
                                               QualityMetric::TetRadiusRatio,
                                               QualityMetric::TetMinDihedralAngle,
                                               QualityMetric::TetMaxDihedralAngle};
    for (const QualityMetric metric : metrics) {
        const auto summary = quality.report().summaries.find(metric);
        REQUIRE(summary != quality.report().summaries.end());
        const Result<ElementId> navigated = quality.worstElementFor(mesh, metric);
        INFO("metric " << meshing::toString(metric));
        REQUIRE(navigated.has_value());
        CHECK(*navigated == summary->second.worst);
        // And it is a real element of this mesh.
        CHECK(mesh.elementType(*navigated).has_value());
    }
}

TEST_CASE("MeshQualityView_RefusesToNavigateIntoADifferentMeshGeneration",
          "[renderer][meshquality][revision]") {
    // THE DEFECT THIS CLASS EXISTS FOR. MeshQualityReport carries no mesh
    // identity, so a report computed on M1 names ElementIds that also exist in
    // M2 and mean different elements there. Without the pairing, navigating
    // "to the worst element" after a remesh would select the wrong tetrahedron
    // and report its quality with total confidence.
    Block block;
    const VolumeMesh first = block.mesh();
    const MeshQualityView quality =
        requireQuality(MeshQualityView::evaluate(first.mesh(), meshing::reportOnlyThresholds()));
    REQUIRE(quality.worstElementFor(first.mesh(), QualityMetric::TetAspectRatio).has_value());

    block.setDepth(14_mm);
    const VolumeMesh second = block.mesh();
    REQUIRE_FALSE(second.mesh().stamp() == first.mesh().stamp());

    // The same ElementId very probably exists in the new mesh. The report is
    // refused anyway, and the message names both generations.
    CHECK_FALSE(quality.describes(second.mesh()));
    const Result<ElementId> refused =
        quality.worstElementFor(second.mesh(), QualityMetric::TetAspectRatio);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("not comparable across generations"));

    CHECK(errorCode(quality.classOf(second.mesh(), ElementId::fromValue(1U))) ==
          ErrorCode::FailedPrecondition);
}

TEST_CASE("MeshQualityView_ClassifiesFromTheReportAndHoldsNoThresholdsOfItsOwn",
          "[renderer][meshquality]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    // A CLASSIFYING POLICY, not reportOnlyThresholds(): see
    // classifyingThresholds() for why that distinction is the whole test.
    const MeshQualityView quality =
        requireQuality(MeshQualityView::evaluate(mesh, classifyingThresholds()));

    // The report must actually have classified something, or what follows
    // proves nothing. This REQUIRE is the guard that the earlier version of
    // this test was missing.
    const meshing::MeshQualityReport& report = quality.report();
    INFO("valid " << report.validElements << ", warning " << report.warningElements
                  << ", failure " << report.failureElements << ", invalid "
                  << report.invalidElements);
    REQUIRE(report.warningElements + report.failureElements > 0U);

    // Every classification the view gives must be the one the report's own
    // findings carry -- recomputed here from the findings by a different
    // route, so the view cannot be agreeing with itself.
    std::map<ElementId, meshing::QualityClass> expected;
    for (const meshing::QualityFinding& finding : report.findings) {
        auto& worst = expected[finding.element];
        if (static_cast<std::uint8_t>(finding.classification) > static_cast<std::uint8_t>(worst)) {
            worst = finding.classification;
        }
    }
    REQUIRE_FALSE(expected.empty());

    std::size_t nonValid = 0U;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const Result<meshing::QualityClass> classification = quality.classOf(mesh, tet.id);
        REQUIRE(classification.has_value());
        const auto found = expected.find(tet.id);
        const meshing::QualityClass wanted =
            (found == expected.end()) ? meshing::QualityClass::Valid : found->second;
        INFO("element " << tet.id);
        CHECK(*classification == wanted);
        if (*classification != meshing::QualityClass::Valid) {
            ++nonValid;
        }
    }
    // AND AT LEAST ONE WAS NOT VALID. Without this, a classOf that answered
    // Valid unconditionally would pass every assertion above.
    CHECK(nonValid > 0U);

    // An element of no mesh is refused rather than classified Valid by
    // default, which would be the comfortable wrong answer.
    CHECK(errorCode(quality.classOf(mesh, ElementId::fromValue(999999U))) == ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// CAD -> mesh, and back
// ---------------------------------------------------------------------------

TEST_CASE("MeshHighlight_ResolvesANamedCadFaceToItsCurrentFacetsAndTriangles",
          "[renderer][meshhighlight][linkage]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const GeometryMeshMap map = block.map(volume);
    const MeshView view = requireView(MeshView::volumeBoundaryOf(mesh));

    Result<MeshHighlight> highlight = renderer::highlightFor(view, mesh, map, block.top());
    REQUIRE(highlight.has_value());
    CHECK(highlight->fullyResolved());
    CHECK_FALSE(highlight->empty());

    // The facets are the mapping's, compared against the canonical query.
    const Result<meshing::BoundaryFacetSet> canonical = meshing::boundaryFacetsOf(map, block.top());
    REQUIRE(canonical.has_value());
    CHECK(std::ranges::equal(highlight->facets(), canonical->facets));

    // Every highlighted triangle belongs to one of those facets -- no leakage
    // into a neighbouring face.
    const std::set<ElementId::ValueType> wanted = [&] {
        std::set<ElementId::ValueType> ids;
        for (const ElementId facet : canonical->facets) {
            ids.insert(facet.value());
        }
        return ids;
    }();
    for (const std::size_t triangle : highlight->triangles) {
        const Result<ElementId> element = view.elementOfTriangle(triangle);
        REQUIRE(element.has_value());
        CHECK(wanted.contains(element->value()));
    }
    // One render triangle per facet, for a boundary view.
    CHECK(highlight->triangles.size() == canonical->facets.size());
}

TEST_CASE("MeshHighlight_PlanarFacesDoNotLeakIntoEachOther",
          "[renderer][meshhighlight][linkage]") {
    // Two different faces of the same block must highlight disjoint facet
    // sets, and together they must not cover the whole boundary.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const GeometryMeshMap map = block.map(volume);
    const MeshView view = requireView(MeshView::volumeBoundaryOf(mesh));

    Result<MeshHighlight> top = renderer::highlightFor(view, mesh, map, block.top());
    Result<MeshHighlight> side = renderer::highlightFor(view, mesh, map, block.side(0));
    REQUIRE(top.has_value());
    REQUIRE(side.has_value());
    REQUIRE_FALSE(top->empty());
    REQUIRE_FALSE(side->empty());

    std::vector<std::size_t> shared;
    std::ranges::set_intersection(top->triangles, side->triangles, std::back_inserter(shared));
    INFO("top " << top->triangles.size() << ", side " << side->triangles.size() << ", shared "
                << shared.size());
    CHECK(shared.empty());
    CHECK(top->triangles.size() + side->triangles.size() < view.triangleCount());
}

TEST_CASE("MeshHighlight_HighlightsAnUnnamedHoleWallByItsPlaceInTheMap",
          "[renderer][meshhighlight][linkage]") {
    // THE MANDATORY REFERENCE CASE, and the reason the face-index overload
    // exists. P16-MAP-001: cutHole names a hole's flat faces and NOT its
    // cylindrical wall, so the wall has no FaceName and cannot be the target
    // of a forward query by name. It is still mapped, so it can be asked for
    // by where it is.
    Bored bored;
    const VolumeMesh volume = bored.mesh();
    const Mesh& mesh = volume.mesh();
    const GeometryMeshMap map = bored.map(volume);
    const MeshView view = requireView(MeshView::volumeBoundaryOf(mesh));

    // Find the cylindrical face with no name: that is the bore wall.
    std::optional<std::size_t> wall;
    for (const meshing::MappedFace& face : map.faces()) {
        if (face.cylinder.has_value() && face.names.empty()) {
            wall = face.index;
        }
    }
    REQUIRE(wall.has_value());
    INFO("the bore wall is face " << *wall << " of " << map.faces().size());

    Result<MeshHighlight> highlight = renderer::highlightFor(view, mesh, map, *wall);
    REQUIRE(highlight.has_value());
    CHECK(highlight->fullyResolved());
    CHECK_FALSE(highlight->empty());
    CHECK(std::ranges::equal(highlight->facets(), map.faces()[*wall].facets));

    // AND IT IS THE INNER WALL, NOT THE OUTER BOUNDARY. Every highlighted
    // facet's nodes lie on the bore's radius, 6 mm from its axis at
    // (20, 15) -- which the outer faces of a 40x30 block cannot satisfy.
    for (const std::size_t triangle : highlight->triangles) {
        const ElementId facet = require(view.elementOfTriangle(triangle));
        const meshing::Triangle* canonical = mesh.findTriangle(facet);
        REQUIRE(canonical != nullptr);
        for (const meshing::NodeId node : canonical->nodes) {
            const meshing::Node* position = mesh.findNode(node);
            REQUIRE(position != nullptr);
            const double dx = position->position.x.si() - 0.020;
            const double dy = position->position.y.si() - 0.015;
            const double radius = std::sqrt((dx * dx) + (dy * dy));
            CHECK(radius < 0.0061);
        }
    }

    // The reverse direction, for a facet of that wall: it resolves back to
    // that same face, and reports that the face has no name rather than
    // inventing one.
    const ElementId facet = map.faces()[*wall].facets.front();
    const Result<meshing::FacetSource> source = renderer::sourceOfFacet(mesh, map, facet);
    REQUIRE(source.has_value());
    CHECK(source->face == *wall);
    CHECK(source->names.empty());
}

TEST_CASE("MeshHighlight_ResolvesAFacetBackToItsNamedCadFace",
          "[renderer][meshhighlight][linkage]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const GeometryMeshMap map = block.map(volume);
    const MeshView view = requireView(MeshView::volumeBoundaryOf(mesh));

    // Forward then back must close the loop for every facet of a named face.
    Result<MeshHighlight> highlight = renderer::highlightFor(view, mesh, map, block.top());
    REQUIRE(highlight.has_value());
    REQUIRE_FALSE(highlight->empty());
    for (const ElementId facet : highlight->facets()) {
        const Result<meshing::FacetSource> source = renderer::sourceOfFacet(mesh, map, facet);
        REQUIRE(source.has_value());
        CHECK(source->facet == facet);
        CHECK(std::ranges::find(source->names, block.top()) != source->names.end());
    }
}

TEST_CASE("MeshHighlight_CarriesUnresolvedRatherThanAnEmptyHighlight",
          "[renderer][meshhighlight][linkage]") {
    // A reference that no longer binds must come back as a STATUS. Flattening
    // it to "nothing highlighted" would make a deleted face indistinguishable
    // from a face that is simply not meshed, and the user would be told
    // nothing at all.
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const GeometryMeshMap map = block.map(volume);
    const MeshView view = requireView(MeshView::volumeBoundaryOf(mesh));

    // A name for a hole that does not exist in this document.
    const FaceName absent{block.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    Result<MeshHighlight> highlight = renderer::highlightFor(view, mesh, map, absent);
    REQUIRE(highlight.has_value());
    CHECK_FALSE(highlight->fullyResolved());
    CHECK(highlight->empty());
    REQUIRE(highlight->mapping.requested.size() == 1U);
    CHECK(highlight->mapping.requested.front().state == meshing::MappingState::Unresolved);
    CHECK(highlight->mapping.requested.front().reference == absent);
}

TEST_CASE("MeshHighlight_RefusesAMappingOrACacheFromADifferentGeneration",
          "[renderer][meshhighlight][revision]") {
    Block block;
    const VolumeMesh first = block.mesh();
    const GeometryMeshMap firstMap = block.map(first);
    const MeshView firstView = requireView(MeshView::volumeBoundaryOf(first.mesh()));

    block.setDepth(14_mm);
    const VolumeMesh second = block.mesh();
    const GeometryMeshMap secondMap = block.map(second);
    const MeshView secondView = requireView(MeshView::volumeBoundaryOf(second.mesh()));

    // A stale MAPPING against the new mesh: refused. Highlighting the old
    // facets against changed CAD is an automatic failure of this milestone.
    CHECK(errorCode(renderer::highlightFor(secondView, second.mesh(), firstMap, block.top())) ==
          ErrorCode::FailedPrecondition);

    // A stale RENDER CACHE against the new mesh: refused, rather than drawing
    // old triangles as though they were current.
    CHECK(errorCode(renderer::highlightFor(firstView, second.mesh(), secondMap, block.top())) ==
          ErrorCode::FailedPrecondition);

    // The reverse query refuses the same way.
    const ElementId facet = first.mesh().triangles().front().id;
    CHECK(errorCode(renderer::sourceOfFacet(second.mesh(), firstMap, facet)) ==
          ErrorCode::FailedPrecondition);

    // Matched generations work, both the old pair and the new pair.
    CHECK(renderer::highlightFor(firstView, first.mesh(), firstMap, block.top()).has_value());
    CHECK(renderer::highlightFor(secondView, second.mesh(), secondMap, block.top()).has_value());
}

TEST_CASE("MeshHighlight_RefusesAFaceIndexTheMapDoesNotHave",
          "[renderer][meshhighlight][validation]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const GeometryMeshMap map = block.map(volume);
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));
    CHECK(errorCode(renderer::highlightFor(view, volume.mesh(), map, map.faces().size())) ==
          ErrorCode::InvalidArgument);
}

TEST_CASE("MeshHighlight_FollowsTheSameFaceOntoARemeshedMesh",
          "[renderer][meshhighlight][linkage]") {
    // A CAD selection survives a remesh and re-highlights through the mapping;
    // the facets themselves do not survive, and must not.
    //
    // THE FIXTURE IS A CURVED FACE ON PURPOSE. A first version of this test
    // refined a BLOCK and asserted the facet count grew -- and it did not,
    // because a plane is exactly representable: SurfaceMeshControls bounds the
    // distance from the TRUE surface, so a planar face is two triangles at any
    // deflection, and volume sizing cannot change a boundary that
    // generateVolumeMesh requires to conform to the surface it was given. The
    // test passed its other assertions and proved nothing about refinement.
    // A cylinder refines; a box cannot.
    Bored bored;
    const meshing::VolumeMeshControls coarseControls{
        .surface = {.linearDeflection = Length::fromSi(5e-4),
                    .angularDeflection = Angle::fromSi(0.8)}};
    const meshing::VolumeMeshControls fineControls{
        .surface = {.linearDeflection = Length::fromSi(5e-5),
                    .angularDeflection = Angle::fromSi(0.15)}};

    const VolumeMesh coarse = bored.mesh(coarseControls);
    const GeometryMeshMap coarseMap = bored.map(coarse);
    const MeshView coarseView = requireView(MeshView::volumeBoundaryOf(coarse.mesh()));
    const std::size_t coarseWall = wallFaceOf(coarseMap);
    Result<MeshHighlight> first =
        renderer::highlightFor(coarseView, coarse.mesh(), coarseMap, coarseWall);
    REQUIRE(first.has_value());
    REQUIRE_FALSE(first->empty());

    const VolumeMesh fine = bored.mesh(fineControls);
    const GeometryMeshMap fineMap = bored.map(fine);
    const MeshView fineView = requireView(MeshView::volumeBoundaryOf(fine.mesh()));
    const std::size_t fineWall = wallFaceOf(fineMap);
    Result<MeshHighlight> second =
        renderer::highlightFor(fineView, fine.mesh(), fineMap, fineWall);
    REQUIRE(second.has_value());
    REQUIRE_FALSE(second->empty());

    // A new mesh generation, and a genuinely finer wall.
    REQUIRE_FALSE(fine.mesh().stamp() == coarse.mesh().stamp());
    INFO("coarse wall " << first->facets().size() << " facets, fine wall "
                        << second->facets().size());
    CHECK(second->facets().size() > first->facets().size());

    // The old facet identities are not authority for the new mesh: the old
    // view and map are refused against it.
    CHECK(errorCode(renderer::highlightFor(coarseView, fine.mesh(), fineMap, fineWall)) ==
          ErrorCode::FailedPrecondition);
    CHECK(errorCode(renderer::highlightFor(fineView, fine.mesh(), coarseMap, fineWall)) ==
          ErrorCode::FailedPrecondition);
}

TEST_CASE("MeshHighlight_ResolvesANamedBoundarySetToItsCurrentFacets",
          "[renderer][meshhighlight][linkage]") {
    // A NAMED SET SURVIVES A REMESH AND ITS FACETS DO NOT. The set stores
    // FaceNames -- the thing that persists -- and resolving it against a mesh
    // produces that mesh's facets. So the same set, resolved against a coarse
    // and then a finer mesh, highlights different facets and still means the
    // same faces.
    Bored bored;
    const meshing::VolumeMeshControls coarse{
        .surface = {.linearDeflection = Length::fromSi(5e-4),
                    .angularDeflection = Angle::fromSi(0.8)}};
    const meshing::VolumeMeshControls fine{
        .surface = {.linearDeflection = Length::fromSi(5e-5),
                    .angularDeflection = Angle::fromSi(0.15)}};

    const VolumeMesh first = bored.mesh(coarse);
    const GeometryMeshMap firstMap = bored.map(first);
    const MeshView firstView = requireView(MeshView::volumeBoundaryOf(first.mesh()));

    // Name the set from a face that HAS a name: a named set is built out of
    // references, and the bore wall has none to give.
    std::optional<FaceName> named;
    for (const meshing::MappedFace& face : firstMap.faces()) {
        if (!face.names.empty() && !face.facets.empty()) {
            named = face.names.front();
            break;
        }
    }
    REQUIRE(named.has_value());

    meshing::NamedBoundarySet set;
    set.id = BoundarySetId::fromValue(1U);
    set.name = "fixed_end";
    set.faces.push_back(*named);
    REQUIRE(meshing::validate(set).has_value());

    Result<MeshHighlight> onCoarse = renderer::highlightFor(firstView, first.mesh(), firstMap, set);
    REQUIRE(onCoarse.has_value());
    CHECK(onCoarse->fullyResolved());
    REQUIRE_FALSE(onCoarse->empty());

    const VolumeMesh second = bored.mesh(fine);
    const GeometryMeshMap secondMap = bored.map(second);
    const MeshView secondView = requireView(MeshView::volumeBoundaryOf(second.mesh()));
    Result<MeshHighlight> onFine =
        renderer::highlightFor(secondView, second.mesh(), secondMap, set);
    REQUIRE(onFine.has_value());
    CHECK(onFine->fullyResolved());
    REQUIRE_FALSE(onFine->empty());

    // The same set, a different mesh, and the facets are the new mesh's.
    REQUIRE_FALSE(second.mesh().stamp() == first.mesh().stamp());
    for (const ElementId facet : onFine->facets()) {
        CHECK(second.mesh().findTriangle(facet) != nullptr);
    }
    // And the old pairing is refused against the new mesh.
    CHECK(errorCode(renderer::highlightFor(firstView, second.mesh(), secondMap, set)) ==
          ErrorCode::FailedPrecondition);
}

// ---------------------------------------------------------------------------
// The invariant
// ---------------------------------------------------------------------------

TEST_CASE("MeshInspection_NothingItDoesChangesTheMesh", "[renderer][meshinspection][readonly]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const Mesh& mesh = volume.mesh();
    const GeometryMeshMap map = block.map(volume);
    const MeshFingerprint before{mesh};
    const GeometryMeshMap mapBefore = map;

    const MeshView view = requireView(MeshView::volumeBoundaryOf(mesh));
    const MeshQualityView quality =
        requireQuality(MeshQualityView::evaluate(mesh, meshing::reportOnlyThresholds()));

    // Everything inspection offers, in one pass.
    (void)renderer::statusOf(block.document, MeshHolding{volume, std::nullopt});
    (void)quality.worstElementFor(mesh, QualityMetric::TetAspectRatio);
    (void)quality.worstElementFor(mesh, QualityMetric::TetRadiusRatio);
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        (void)quality.classOf(mesh, tet.id);
    }
    (void)renderer::highlightFor(view, mesh, map, block.top());
    (void)renderer::highlightFor(view, mesh, map, block.side(0));
    (void)renderer::highlightFor(view, mesh, map, std::size_t{0U});
    for (const meshing::Triangle& facet : mesh.triangles()) {
        (void)renderer::sourceOfFacet(mesh, map, facet.id);
    }

    CHECK(MeshFingerprint{mesh} == before);
    // And the mapping is untouched: a highlight reads it and never rebinds it.
    CHECK(map == mapBefore);
}
