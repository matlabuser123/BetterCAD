// P16-VIZ-001: displaying an engineering mesh in the 3D view.
//
// NEEDS A GL IMPLEMENTATION, unlike MeshViewTests.cpp and
// MeshInspectionTests.cpp, which are headless. The split is deliberate: the
// adapter's logic is where the engineering content is and it is testable
// anywhere, while this file tests that the adapter's buffers actually reach a
// view. A machine with no usable OpenGL fails these and still runs the others.
//
// MEASURED BY COVERAGE, never by screenshots: how many pixels differ from the
// view's background. A ToPixMap that succeeded proves nothing -- a view that
// drew nothing returns success and a background-coloured image.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/renderer/MeshInspection.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/renderer/Viewer.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using meshing::ElementId;
using meshing::VolumeMesh;
using renderer::MeshPick;
using renderer::MeshStyle;
using renderer::MeshView;
using renderer::PresentationId;
using renderer::PresentationKind;
using renderer::RenderedImage;
using renderer::Rgb;
using renderer::Viewer;

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 256;
constexpr Rgb kBlack{0, 0, 0};

struct Block {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};

    /// @p plane and @p origin move the solid away from the obvious case: a
    /// block at the origin on the XY plane would overlay its mesh correctly
    /// even if a frame were being dropped somewhere, because every frame
    /// agrees at the origin.
    explicit Block(const Frame3D& plane = Frame3D::xy(), Length originX = 0_mm,
                   Length originY = 0_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", plane);
        (void)addRectangle(*sketch, originX, originY, originX + 30_mm, originY + 20_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] VolumeMesh mesh(const meshing::VolumeMeshControls& controls = {}) {
        Result<VolumeMesh> result =
            meshing::volumeMeshFor(document, regenerator, feature, controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }

    [[nodiscard]] geometry::Body body() {
        const geometry::Body* found = regenerator.body(feature);
        REQUIRE(found != nullptr);
        return *found;
    }
};

/// A block with a drilled through hole. Its CYLINDRICAL WALL is what makes
/// this fixture necessary: a box's faces are planar and tessellate to the same
/// twelve triangles at any deflection, so a box cannot distinguish the
/// engineering mesh from the kernel's display triangulation.
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
};

[[nodiscard]] Viewer makeViewer() {
    Result<Viewer> viewer = Viewer::createOffscreen(kWidth, kHeight);
    if (!viewer.has_value()) {
        FAIL("the offscreen viewer could not be created: " << viewer.error().message);
    }
    viewer->setBackground(kBlack);
    return std::move(*viewer);
}

[[nodiscard]] MeshView requireView(Result<MeshView> view) {
    if (!view.has_value()) {
        FAIL("the mesh view was refused: " << view.error().message);
    }
    return std::move(*view);
}

[[nodiscard]] RenderedImage renderOf(Viewer& viewer) {
    Result<RenderedImage> image = viewer.render();
    if (!image.has_value()) {
        FAIL("the view could not be rendered: " << image.error().message);
    }
    return std::move(*image);
}

struct MeshFingerprint {
    meshing::MeshStamp stamp{};
    std::vector<meshing::Node> nodes{};
    std::vector<meshing::Triangle> triangles{};
    std::vector<meshing::Tetrahedron> tetrahedra{};

    explicit MeshFingerprint(const meshing::Mesh& mesh)
        : stamp(mesh.stamp()), nodes(mesh.nodes().begin(), mesh.nodes().end()),
          triangles(mesh.triangles().begin(), mesh.triangles().end()),
          tetrahedra(mesh.tetrahedra().begin(), mesh.tetrahedra().end()) {}

    friend bool operator==(const MeshFingerprint&, const MeshFingerprint&) = default;
};

} // namespace

TEST_CASE("MeshDisplay_TheMeshOverlaysItsOwnCadSolidAtTheSameScale",
          "[renderer][meshdisplay][frame]") {
    // THE UNIT TEST IN THE LITERAL SENSE, and it caught a defect that made the
    // whole feature useless in the GUI.
    //
    // A boundary mesh of a solid has the SAME SILHOUETTE as the solid. So from
    // a fixed camera, the mesh alone, the solid alone, and both together must
    // cover the SAME NUMBER OF PIXELS. Any scale or frame error between the
    // two shows up immediately as three different numbers.
    //
    // It did: MeshView positions are SI metres, OCCT model space is
    // millimetres, and the mesh was going in 1000 times too small. Alone it
    // looked perfect -- FitAll framed the only thing in the scene -- and beside
    // its own solid it was a sub-pixel speck. The mesh measured 32674 pixels,
    // the two together 54120: one silhouette, two scales.
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));

    const auto coverageOf = [&block, &view](bool withCad, bool withMesh) {
        Viewer viewer = makeViewer();
        if (withCad) {
            (void)require(viewer.display(block.feature, block.body()));
        }
        if (withMesh) {
            (void)require(viewer.displayMesh(block.feature, view, MeshStyle::Shaded));
        }
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderOf(viewer).coverage(kBlack);
    };

    const std::size_t cadOnly = coverageOf(true, false);
    const std::size_t meshOnly = coverageOf(false, true);
    const std::size_t together = coverageOf(true, true);
    INFO("cad " << cadOnly << ", mesh " << meshOnly << ", together " << together);
    REQUIRE(cadOnly > 0U);
    CHECK(meshOnly == cadOnly);
    CHECK(together == cadOnly);
}

TEST_CASE("MeshDisplay_DrawsAnEngineeringMeshIntoTheView", "[renderer][meshdisplay]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));

    Viewer viewer = makeViewer();
    CHECK(renderOf(viewer).coverage(kBlack) == 0U);

    const PresentationId mesh = require(viewer.displayMesh(block.feature, view));
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    const std::size_t covered = renderOf(viewer).coverage(kBlack);
    INFO("covered " << covered << " of " << (kWidth * kHeight));
    CHECK(covered > 0U);
    CHECK(covered < static_cast<std::size_t>(kWidth) * kHeight);

    // One batched presentation for the whole mesh, not one per element.
    REQUIRE(viewer.displayed().size() == 1U);
    CHECK(viewer.displayed().front().kind == PresentationKind::EngineeringMesh);
    CHECK(viewer.displayed().front().object == block.feature);
    // And it knows which generation it is showing.
    CHECK(require(viewer.meshStampOf(mesh)) == volume.mesh().stamp());
}

TEST_CASE("MeshDisplay_HidingTheMeshLeavesTheCadBodyAndViceVersa",
          "[renderer][meshdisplay][visibility]") {
    // Two presentations of the same feature: the solid and its mesh. Hiding
    // one must not touch the other, and neither must be deleted.
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));

    Viewer viewer = makeViewer();
    const PresentationId cad = require(viewer.display(block.feature, block.body()));
    const PresentationId mesh =
        require(viewer.displayMesh(block.feature, view, MeshStyle::Wireframe));
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();
    const std::size_t both = renderOf(viewer).coverage(kBlack);
    REQUIRE(both > 0U);

    REQUIRE(viewer.setVisible(mesh, false).has_value());
    const std::size_t cadOnly = renderOf(viewer).coverage(kBlack);
    CHECK(cadOnly > 0U);
    // Hidden is not deleted: both presentations are still listed.
    CHECK(viewer.displayed().size() == 2U);

    REQUIRE(viewer.setVisible(cad, false).has_value());
    CHECK(renderOf(viewer).coverage(kBlack) == 0U);
    CHECK(viewer.displayed().size() == 2U);

    // Showing the mesh again brings back the mesh, not the solid.
    REQUIRE(viewer.setVisible(mesh, true).has_value());
    const std::size_t meshOnly = renderOf(viewer).coverage(kBlack);
    CHECK(meshOnly > 0U);
    CHECK(meshOnly != cadOnly);
}

TEST_CASE("MeshDisplay_StyleChangesTheImageAndNotTheMesh", "[renderer][meshdisplay][wireframe]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));
    const MeshFingerprint before{volume.mesh()};

    Viewer viewer = makeViewer();
    const PresentationId mesh =
        require(viewer.displayMesh(block.feature, view, MeshStyle::Shaded));
    viewer.setStandardView(renderer::StandardView::Top);
    viewer.fitAll();
    const std::size_t shaded = renderOf(viewer).coverage(kBlack);
    REQUIRE(shaded > 0U);
    CHECK(require(viewer.meshStyleOf(mesh)) == MeshStyle::Shaded);

    // Wireframe draws strictly less than shaded: edges only, not filled
    // facets. This is the assertion that would notice a style switch doing
    // nothing.
    REQUIRE(viewer.setMeshStyle(mesh, MeshStyle::Wireframe).has_value());
    const std::size_t wireframe = renderOf(viewer).coverage(kBlack);
    INFO("shaded " << shaded << ", wireframe " << wireframe);
    CHECK(wireframe > 0U);
    CHECK(wireframe < shaded);
    CHECK(require(viewer.meshStyleOf(mesh)) == MeshStyle::Wireframe);

    REQUIRE(viewer.setMeshStyle(mesh, MeshStyle::ShadedWithEdges).has_value());
    CHECK(renderOf(viewer).coverage(kBlack) > 0U);

    // Toggling a display mode is a VISUAL operation: no regeneration, no
    // change to the canonical mesh.
    CHECK(MeshFingerprint{volume.mesh()} == before);
}

TEST_CASE("MeshDisplay_HighlightingChangesTheImageAndNotTheMesh",
          "[renderer][meshdisplay][selection]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));
    const MeshFingerprint before{volume.mesh()};

    Viewer viewer = makeViewer();
    const PresentationId mesh =
        require(viewer.displayMesh(block.feature, view, MeshStyle::Shaded));
    viewer.setStandardView(renderer::StandardView::Top);
    viewer.fitAll();
    const RenderedImage plain = renderOf(viewer);
    REQUIRE(plain.coverage(kBlack) > 0U);
    CHECK(viewer.meshHighlightOf(mesh).has_value());
    CHECK(require(viewer.meshHighlightOf(mesh)).empty());

    // HIGHLIGHT EVERY FACET, not a subset. A first version of this test
    // highlighted every other triangle by index and then asserted the image
    // changed -- which it need not: from a top view only the top face's two
    // facets of twelve are visible, and whether either of them has an even
    // index is an accident of the mesher's ordering. The test would have
    // passed or failed on that accident. Highlighting all of them makes
    // whatever is visible change colour whatever the ordering is.
    //
    // Compared through a bool rather than CHECK_FALSE(a == b): Catch2 would
    // otherwise stringify two 196608-element vectors into the failure output.
    std::vector<std::size_t> everything;
    for (std::size_t triangle = 0U; triangle < view.triangleCount(); ++triangle) {
        everything.push_back(triangle);
    }
    REQUIRE(viewer.setMeshHighlight(mesh, everything).has_value());
    const RenderedImage lit = renderOf(viewer);
    CHECK(require(viewer.meshHighlightOf(mesh)).size() == everything.size());
    const bool highlightChangedTheImage = lit.rgb != plain.rgb;
    CHECK(highlightChangedTheImage);
    // The same facets are drawn either way, so the covered area is unchanged:
    // a highlight recolours, it does not add or remove geometry.
    CHECK(lit.coverage(kBlack) == plain.coverage(kBlack));

    // Clearing it restores the earlier image exactly.
    REQUIRE(viewer.setMeshHighlight(mesh, {}).has_value());
    const bool clearingRestoredTheImage = renderOf(viewer).rgb == plain.rgb;
    CHECK(clearingRestoredTheImage);

    CHECK(MeshFingerprint{volume.mesh()} == before);
}

TEST_CASE("MeshDisplay_RefusesAHighlightOutsideTheRenderBuffer",
          "[renderer][meshdisplay][validation]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));

    Viewer viewer = makeViewer();
    const PresentationId mesh = require(viewer.displayMesh(block.feature, view));
    const std::vector<std::size_t> past{view.triangleCount()};
    CHECK(errorCode(viewer.setMeshHighlight(mesh, past)) == ErrorCode::InvalidArgument);
}

TEST_CASE("MeshDisplay_PickingAMeshResolvesToACanonicalElement",
          "[renderer][meshdisplay][selection]") {
    // THE BRIDGE, END TO END: a pixel, to a render triangle, to an ElementId
    // the canonical mesh recognises. The viewer deliberately stops at the
    // render triangle -- it has no MeshView and no mesh -- and the caller does
    // the translation, which is the only place it can be done correctly.
    Block block;
    const VolumeMesh volume = block.mesh();
    const meshing::Mesh& canonical = volume.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(canonical));

    Viewer viewer = makeViewer();
    const PresentationId mesh =
        require(viewer.displayMesh(block.feature, view, MeshStyle::Shaded));
    viewer.setStandardView(renderer::StandardView::Top);
    viewer.fitAll();
    REQUIRE(renderOf(viewer).coverage(kBlack) > 0U);

    // Scan rather than naming pixels: fitAll decides the layout, so a fixed
    // coordinate would be asserting about the camera.
    std::set<ElementId::ValueType> hit;
    for (int y = kHeight / 4; y < (3 * kHeight) / 4; y += 8) {
        for (int x = kWidth / 4; x < (3 * kWidth) / 4; x += 8) {
            const Result<std::optional<MeshPick>> picked = viewer.pickMeshAt(x, y);
            REQUIRE(picked.has_value());
            if (!picked->has_value()) {
                continue;
            }
            CHECK((*picked)->presentation == mesh);
            // The translation, through the view that built the presentation.
            const Result<ElementId> element = view.elementOfTriangle((*picked)->triangle);
            REQUIRE(element.has_value());
            // And it is a triangle the canonical mesh has.
            CHECK(canonical.findTriangle(*element) != nullptr);
            hit.insert(element->value());
        }
    }
    INFO("distinct facets picked: " << hit.size());
    CHECK_FALSE(hit.empty());
}

TEST_CASE("MeshDisplay_PickingAMeshFindsNothingWhereThereIsNoMesh",
          "[renderer][meshdisplay][selection]") {
    Block block;
    Viewer viewer = makeViewer();
    // An empty view: nothing to pick, and that is not an error.
    const Result<std::optional<MeshPick>> picked = viewer.pickMeshAt(kWidth / 2, kHeight / 2);
    REQUIRE(picked.has_value());
    CHECK_FALSE(picked->has_value());

    // A CAD body under the cursor is not a mesh pick. pickMeshAt answers
    // nothing rather than the nearest mesh.
    const PresentationId cad = require(viewer.display(block.feature, block.body()));
    (void)cad;
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();
    REQUIRE(renderOf(viewer).coverage(kBlack) > 0U);
    bool anyMeshPick = false;
    for (int x = kWidth / 4; x < (3 * kWidth) / 4; x += 16) {
        const Result<std::optional<MeshPick>> onCad = viewer.pickMeshAt(x, kHeight / 2);
        REQUIRE(onCad.has_value());
        anyMeshPick = anyMeshPick || onCad->has_value();
    }
    CHECK_FALSE(anyMeshPick);
}

TEST_CASE("MeshDisplay_RefusesMeshOperationsOnACadPresentation",
          "[renderer][meshdisplay][validation]") {
    Block block;
    Viewer viewer = makeViewer();
    const PresentationId cad = require(viewer.display(block.feature, block.body()));

    // Each refusal names what the presentation IS, not only what it is not.
    const Result<MeshStyle> style = viewer.meshStyleOf(cad);
    REQUIRE_FALSE(style.has_value());
    CHECK(style.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(style.error().message, ContainsSubstring("a CAD body"));

    CHECK(errorCode(viewer.meshStampOf(cad)) == ErrorCode::FailedPrecondition);
    CHECK(errorCode(viewer.setMeshStyle(cad, MeshStyle::Wireframe)) ==
          ErrorCode::FailedPrecondition);
    CHECK(errorCode(viewer.setMeshHighlight(cad, {})) == ErrorCode::FailedPrecondition);

    // And a presentation that is not displayed at all.
    CHECK(errorCode(viewer.meshStampOf(PresentationId::fromValue(999U))) == ErrorCode::NotFound);
}

TEST_CASE("MeshDisplay_TheMeshOverlaysACadSolidThatIsNotAtTheOrigin",
          "[renderer][meshdisplay][frame]") {
    // THE FRAME TEST, and the reason it uses an offset block on a
    // non-default plane: at the origin on the XY plane, every candidate frame
    // agrees, so a dropped or confused transform would still overlay
    // perfectly and the test would pass while proving nothing.
    //
    // A mesh carries NO transform -- ADR-032: "a mesh is in its body's
    // coordinate frame and carries no transform" -- so the only way its
    // coordinates can disagree with the solid's is if this adapter put them in
    // the wrong frame or the wrong unit. The silhouette comparison catches
    // both: same box, same camera, so same pixel count.
    //
    // The XZ plane is chosen deliberately: its normal faces -Y, which is the
    // asymmetry most likely to expose a sign error.
    struct Case {
        const char* name;
        Frame3D plane;
        Length x;
        Length y;
    };
    const std::array<Case, 3> cases{
        Case{"XY at the origin", Frame3D::xy(), 0_mm, 0_mm},
        Case{"XY offset by (50, 40) mm", Frame3D::xy(), 50_mm, 40_mm},
        Case{"XZ offset by (50, 40) mm", Frame3D::xz(), 50_mm, 40_mm},
    };

    for (const Case& which : cases) {
        Block block{which.plane, which.x, which.y};
        const VolumeMesh volume = block.mesh();
        const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));

        const auto coverageOf = [&block, &view](bool withCad, bool withMesh) {
            Viewer viewer = makeViewer();
            if (withCad) {
                (void)require(viewer.display(block.feature, block.body()));
            }
            if (withMesh) {
                (void)require(viewer.displayMesh(block.feature, view, MeshStyle::Shaded));
            }
            viewer.setStandardView(renderer::StandardView::Isometric);
            viewer.fitAll();
            return renderOf(viewer).coverage(kBlack);
        };

        const std::size_t cadOnly = coverageOf(true, false);
        const std::size_t meshOnly = coverageOf(false, true);
        const std::size_t together = coverageOf(true, true);
        INFO(which.name << ": cad " << cadOnly << ", mesh " << meshOnly << ", together "
                        << together);
        REQUIRE(cadOnly > 0U);
        CHECK(meshOnly == cadOnly);
        CHECK(together == cadOnly);
    }
}

TEST_CASE("MeshDisplay_IsUnaffectedByTheCadDisplayTessellation",
          "[renderer][meshdisplay][independence]") {
    // THE ENGINEERING MESH IS NOT THE VIEWER'S TRIANGULATION. Presenting the
    // kernel's display tessellation as the engineering mesh is an automatic
    // failure of this milestone, so the two must be shown to be unrelated.
    //
    // TWO THINGS HAD TO BE GOT RIGHT BEFORE THIS TEST MEANT ANYTHING.
    //
    // 1. The body needs a CURVED face. A box's faces are planar, every
    //    tessellation of them is the same twelve triangles, and a MeshView
    //    secretly built from the display triangulation would have produced
    //    byte-identical buffers and passed.
    //
    // 2. The requested deflection has to be FINER than the engineering mesh's.
    //    Measured on this body: asking for 5 mm gives 160 triangles -- exactly
    //    the engineering boundary's 160 -- because OCCT does not COARSEN an
    //    existing triangulation, it keeps the finer one it already has. So a
    //    coarse request is a no-op and tests nothing. Asking for 0.01 mm gives
    //    324, which is a genuinely different triangulation of the same body.
    Bored bored;
    const VolumeMesh volume = bored.mesh();
    const MeshView before = requireView(MeshView::volumeBoundaryOf(volume.mesh()));

    const Result<meshing::MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(bored.document, bored.regenerator, bored.bore);
    REQUIRE(prepared.has_value());
    const Result<geometry::Mesh> display =
        geometry::triangulate(prepared->body, {.linearDeflection = Length::fromSi(1e-5)});
    REQUIRE(display.has_value());
    CHECK_FALSE(display->triangles.empty());

    // The two ARE different triangulations of the same body, so the equality
    // below is not trivially true.
    INFO("engineering boundary " << before.triangleCount() << " facets, display tessellation "
                                 << display->triangles.size() << " triangles");
    CHECK(display->triangles.size() > before.triangleCount());

    // And asking the kernel for one leaves the engineering mesh's buffers
    // untouched, byte for byte.
    const MeshView after = requireView(MeshView::volumeBoundaryOf(volume.mesh()));
    CHECK(after.triangleCount() == before.triangleCount());
    CHECK(after.vertexCount() == before.vertexCount());
    CHECK(after.edgeCount() == before.edgeCount());
    CHECK(std::ranges::equal(after.positions(), before.positions()));
    CHECK(std::ranges::equal(after.triangleIndices(), before.triangleIndices()));
}

TEST_CASE("MeshDisplay_AStaleMeshDoesNotLookLikeACurrentOne",
          "[renderer][meshdisplay][meshstate]") {
    // THE GATE CLAUSE, MEASURED. "A stale mesh must not look identical to a
    // current mesh" is a claim about pixels, and a claim about pixels can be
    // checked: the same mesh, the same camera, drawn current and drawn stale,
    // must differ. What must NOT differ is the covered area -- staleness is a
    // recolouring, not a change of geometry, because the stale mesh is still
    // the mesh that was generated and inspecting it must still be possible.
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));

    Viewer viewer = makeViewer();
    const PresentationId mesh =
        require(viewer.displayMesh(block.feature, view, MeshStyle::ShadedWithEdges));
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    CHECK_FALSE(require(viewer.meshIsStale(mesh)));
    const RenderedImage current = renderOf(viewer);
    REQUIRE(current.coverage(kBlack) > 0U);

    REQUIRE(viewer.setMeshStale(mesh, true).has_value());
    CHECK(require(viewer.meshIsStale(mesh)));
    const RenderedImage stale = renderOf(viewer);
    const bool staleLooksDifferent = stale.rgb != current.rgb;
    CHECK(staleLooksDifferent);
    CHECK(stale.coverage(kBlack) == current.coverage(kBlack));

    // And back: a regenerated mesh must not keep looking stale.
    REQUIRE(viewer.setMeshStale(mesh, false).has_value());
    const bool currentAgain = renderOf(viewer).rgb == current.rgb;
    CHECK(currentAgain);
}

TEST_CASE("MeshDisplay_InteriorInspectionDrawsTheChosenTetrahedraOnly",
          "[renderer][meshdisplay][interior]") {
    // Interior inspection is optional in the checklist and is IMPLEMENTED
    // rather than deferred: a selected tetrahedron's four faces are a view of
    // their own, so a user can see inside without a clipping plane and without
    // the whole volume being turned into geometry.
    Block block;
    const VolumeMesh volume = block.mesh();
    const meshing::Mesh& mesh = volume.mesh();
    REQUIRE_FALSE(mesh.tetrahedra().empty());

    const std::array<meshing::ElementId, 1> one{mesh.tetrahedra().front().id};
    const MeshView interior = requireView(MeshView::tetrahedraOf(mesh, one));
    const MeshView boundary = requireView(MeshView::volumeBoundaryOf(mesh));

    Viewer viewer = makeViewer();
    const PresentationId shown =
        require(viewer.displayMesh(block.feature, interior, MeshStyle::ShadedWithEdges));
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();
    const std::size_t oneTet = renderOf(viewer).coverage(kBlack);
    CHECK(oneTet > 0U);
    CHECK(require(viewer.meshStampOf(shown)) == mesh.stamp());

    // One tetrahedron covers less of the view than the whole boundary does,
    // from the same camera -- which is what makes it an inspection of a part
    // rather than of everything.
    Viewer whole = makeViewer();
    (void)require(whole.displayMesh(block.feature, boundary, MeshStyle::ShadedWithEdges));
    whole.setStandardView(renderer::StandardView::Isometric);
    whole.fitAll();
    const RenderedImage wholeImage = require(whole.render());
    INFO("one tetrahedron " << oneTet << ", whole boundary " << wholeImage.coverage(kBlack));
    CHECK(interior.triangleCount() == 4U);
    CHECK(interior.triangleCount() < boundary.triangleCount());
}

TEST_CASE("MeshDisplay_RemainsOneBatchedPresentationOnAModeratelyLargeMesh",
          "[renderer][meshdisplay][scale]") {
    // THE PATHOLOGICAL CASE THE BRIEF FORBIDS is one interactive object per
    // element: a mesh of a few thousand tetrahedra displayed as a few thousand
    // AIS objects spends its time in the presentation manager and is unusable.
    //
    // That is prevented by construction here -- the facets go into batched
    // Graphic3d arrays and the whole mesh is ONE selectable object -- so what
    // this test does is confirm the construction holds at a size where the
    // difference would matter, and that the operations the brief lists still
    // work there: show, hide, wireframe, select an element, navigate quality.
    //
    // NOT A PERFORMANCE TEST. No timing is asserted: a wall-clock bound would
    // be a bound on this machine's GPU, and the brief explicitly asks for no
    // GPU optimisation yet. What is asserted is that the object count stays at
    // one and the behaviour stays correct.
    Bored block;
    const VolumeMesh volume = block.mesh({.surface = {.linearDeflection = Length::fromSi(2e-5),
                                                      .angularDeflection = Angle::fromSi(0.12)},
                                          .sizing = {.globalTargetSize = 2_mm}});
    const meshing::Mesh& mesh = volume.mesh();
    INFO("nodes " << mesh.nodeCount() << ", tetrahedra " << mesh.tetrahedra().size()
                  << ", boundary facets " << mesh.triangles().size());
    // Moderately large: enough that a per-element presentation would be
    // obviously wrong, while still quick to build. Measured on this fixture:
    // 261 nodes, 575 tetrahedra, 436 boundary facets.
    //
    // THE FIXTURE IS CURVED, AND A BLOCK WOULD NOT DO. P16-SIZE-001 states
    // why: "OCCT triangulates a PLANAR face with two triangles whatever the
    // deflection -- a 40 mm block's boundary is irreducibly 40 mm, so a global
    // target of 20 or 10 mm has nowhere to act", because generateVolumeMesh
    // requires the volume boundary to CONFORM to the surface it was given and
    // the backend only fills it. A block asked for 1.5 mm elements comes back
    // with twelve, and the resolved sizing record confirms the 1.5 mm was
    // received and used. Refining the boundary is SurfaceMeshControls' job and
    // only a curved wall responds to it.
    REQUIRE(mesh.tetrahedra().size() > 500U);

    const MeshView view = requireView(MeshView::volumeBoundaryOf(mesh));
    CHECK(view.triangleCount() == volume.boundaryTriangleCount());

    Viewer viewer = makeViewer();
    const PresentationId presentation =
        require(viewer.displayMesh(block.bore, view, MeshStyle::ShadedWithEdges));
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    // ONE presentation, whatever the element count.
    REQUIRE(viewer.displayed().size() == 1U);
    CHECK(viewer.displayed().front().kind == PresentationKind::EngineeringMesh);

    const std::size_t shaded = renderOf(viewer).coverage(kBlack);
    CHECK(shaded > 0U);

    // Hide, show: the operations the brief names, at this size.
    REQUIRE(viewer.setVisible(presentation, false).has_value());
    CHECK(renderOf(viewer).coverage(kBlack) == 0U);
    REQUIRE(viewer.setVisible(presentation, true).has_value());
    CHECK(renderOf(viewer).coverage(kBlack) == shaded);

    // Wireframe.
    REQUIRE(viewer.setMeshStyle(presentation, MeshStyle::Wireframe).has_value());
    const std::size_t wireframe = renderOf(viewer).coverage(kBlack);
    CHECK(wireframe > 0U);
    CHECK(wireframe < shaded);
    REQUIRE(viewer.setMeshStyle(presentation, MeshStyle::ShadedWithEdges).has_value());

    // Select one element out of many, and highlight only it.
    const meshing::ElementId facet = mesh.triangles().front().id;
    const std::vector<std::size_t> triangles = view.trianglesOfElement(facet);
    REQUIRE(triangles.size() == 1U);
    REQUIRE(viewer.setMeshHighlight(presentation, triangles).has_value());
    CHECK(require(viewer.meshHighlightOf(presentation)).size() == 1U);

    // And quality navigation over the whole report.
    const Result<renderer::MeshQualityView> quality =
        renderer::MeshQualityView::evaluate(mesh, meshing::reportOnlyThresholds());
    REQUIRE(quality.has_value());
    const Result<meshing::ElementId> worst =
        quality->worstElementFor(mesh, meshing::QualityMetric::TetAspectRatio);
    REQUIRE(worst.has_value());
    CHECK(mesh.elementType(*worst).has_value());
}

TEST_CASE("MeshDisplay_RefusesAnEmptyMeshViewAndAnInvalidSource",
          "[renderer][meshdisplay][validation]") {
    Block block;
    const VolumeMesh volume = block.mesh();
    const MeshView view = requireView(MeshView::volumeBoundaryOf(volume.mesh()));
    Viewer viewer = makeViewer();
    CHECK(errorCode(viewer.displayMesh(ObjectId{}, view)) == ErrorCode::InvalidArgument);
}
