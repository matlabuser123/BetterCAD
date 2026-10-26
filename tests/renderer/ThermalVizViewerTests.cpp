// P18-VIZ-001 -- the thermal view on a screen: the unit boundary, the contour, the stale state, the overlays, picking.
//
// THE CASES TAGGED [frame] NEED A GPU, and are kept apart for that reason: a driver is not an engineering gate, so
// everything numerical is asserted headlessly in the other files, and these measure what a headless test cannot --
// that a buffer in SI metres reaches OCCT model space at the right SCALE, that a stale result is not drawn as a
// current one, and that a pick resolves back to an engineering element. Pixel counts supplement; they never stand in for
// the numerical evidence of the headless tests.
//
// THE UNIT BOUNDARY IS MEASURED THE WAY P16-VIZ-001 FOUND ITS DEFECT. A thermal overlay leaves this layer in metres and
// reaches millimetres through the one conversion in the presentation adapter. Dropped, the overlay is 1000 times too
// small -- and displayed ALONE it looks perfect, because fitAll frames the only thing in the scene. So it is displayed
// BESIDE a CAD solid whose size the kernel supplies, from ONE camera, and the two silhouettes must coincide.

#include "ThermalVizTestSupport.hpp"

#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/renderer/Viewer.hpp>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <string>

using namespace vztest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
const renderer::Rgb kBlack{0, 0, 0};

[[nodiscard]] renderer::Viewer makeViewer() {
    Result<renderer::Viewer> viewer = renderer::Viewer::createOffscreen(kWidth, kHeight);
    if (!viewer.has_value()) {
        FAIL("the offscreen viewer could not be created: " << viewer.error().message);
    }
    viewer->setBackground(kBlack);
    return std::move(*viewer);
}

[[nodiscard]] renderer::RenderedImage renderOf(renderer::Viewer& viewer) {
    Result<renderer::RenderedImage> image = viewer.render();
    if (!image.has_value()) {
        FAIL("the view could not be rendered: " << image.error().message);
    }
    return std::move(*image);
}

struct Box {
    int x0 = 1 << 20;
    int y0 = 1 << 20;
    int x1 = -(1 << 20);
    int y1 = -(1 << 20);
    std::size_t pixels = 0;
};

/// The bounding box of every pixel that differs from the black background.
[[nodiscard]] Box boundsOf(const renderer::RenderedImage& image, int tolerance = 6) {
    Box box;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const renderer::Rgb p = image.at(x, y);
            if (p.red > tolerance || p.green > tolerance || p.blue > tolerance) {
                box.x0 = std::min(box.x0, x);
                box.y0 = std::min(box.y0, y);
                box.x1 = std::max(box.x1, x);
                box.y1 = std::max(box.y1, y);
                ++box.pixels;
            }
        }
    }
    return box;
}

[[nodiscard]] std::size_t distinctColours(const renderer::RenderedImage& image) {
    std::set<std::array<std::uint8_t, 3>> seen;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const renderer::Rgb p = image.at(x, y);
            if (p.red != 0 || p.green != 0 || p.blue != 0) {
                seen.insert({p.red, p.green, p.blue});
            }
        }
    }
    return seen.size();
}

/// The twelve edges of an axis-aligned box from the origin to (x, y, z) metres, as an overlay.
[[nodiscard]] renderer::ThermalOverlay boxEdgesOverlay(double x, double y, double z) {
    const auto p = [](double a, double b, double c) { return Point3D{Length::fromSi(a), Length::fromSi(b), Length::fromSi(c)}; };
    const std::array<std::array<double, 3>, 8> corners{{{0, 0, 0}, {x, 0, 0}, {x, y, 0}, {0, y, 0}, {0, 0, z}, {x, 0, z}, {x, y, z}, {0, y, z}}};
    const std::array<std::array<std::size_t, 2>, 12> edges{{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    renderer::ThermalOverlay overlay;
    overlay.role = renderer::ThermalOverlayRole::HeatFluxVectors;
    for (const auto& e : edges) {
        overlay.segments.push_back({p(corners[e[0]][0], corners[e[0]][1], corners[e[0]][2]), p(corners[e[1]][0], corners[e[1]][1], corners[e[1]][2])});
    }
    overlay.glyphs = overlay.segments.size();
    return overlay;
}

struct SmallBox {
    Document document{"Box"};
    features::Regenerator regenerator;
    ObjectId feature{};

    SmallBox(Length width, Length height, Length depth) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, width, height);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create("Solid", {.profile = SketchId::fromValue(profile.value()), .depth = depth});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }
    [[nodiscard]] const geometry::Body& body() const {
        const geometry::Body* held = regenerator.body(feature);
        REQUIRE(held != nullptr);
        return *held;
    }
};

} // namespace

// ===========================================================================
// SI metres -> OCCT millimetres, exactly once
// ===========================================================================

TEST_CASE("ThermalViz_SIMetreToOCCTMillimetre", "[thermal][viz][units][frame]") {
    // THE BRIEF'S SAMPLE: [0.001, 0.002, -0.003] m is [1, 2, -3] mm. The kernel supplies the millimetres: a CAD box of
    // 1 x 2 x 3 mm. The overlay is the same box traced in SI METRES -- 0.001 x 0.002 x 0.003 -- and the two must have the
    // same silhouette from one camera. A missing conversion makes the overlay a speck a thousand times too small; a double
    // conversion makes it a thousand times too large and past the frame.
    //
    // AND THE SIGN: a second overlay traces the box toward -z (the sample's own z = -0.003). It is the mirror image of the
    // solid across its base and must NOT coincide with the solid's silhouette -- a lost sign would put it there.
    const SmallBox cad(1_mm, 2_mm, 3_mm);
    const renderer::ThermalOverlay positive = boxEdgesOverlay(0.001, 0.002, 0.003);
    const renderer::ThermalOverlay negative = boxEdgesOverlay(0.001, 0.002, -0.003);
    renderer::Viewer viewer = makeViewer();
    const Result<renderer::PresentationId> body = viewer.display(cad.feature, cad.body());
    REQUIRE(body.has_value());
    const Result<renderer::PresentationId> plus = viewer.displayOverlay(cad.feature, positive);
    REQUIRE(plus.has_value());
    const Result<renderer::PresentationId> minus = viewer.displayOverlay(cad.feature, negative);
    REQUIRE(minus.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();   // ONE camera for all three, framed once with everything in the scene

    const auto only = [&](bool showBody, bool showPlus, bool showMinus) {
        REQUIRE(viewer.setVisible(*body, showBody).has_value());
        REQUIRE(viewer.setVisible(*plus, showPlus).has_value());
        REQUIRE(viewer.setVisible(*minus, showMinus).has_value());
        return boundsOf(renderOf(viewer));
    };
    const Box cadBox = only(true, false, false);
    const Box plusBox = only(false, true, false);
    const Box minusBox = only(false, false, true);

    // NON-VACUITY FIRST: all three are really on screen, with substance, and not the whole frame.
    REQUIRE(cadBox.pixels > 500U);
    REQUIRE(plusBox.pixels > 100U);
    REQUIRE(minusBox.pixels > 100U);
    INFO("CAD " << cadBox.x0 << ".." << cadBox.x1 << " x " << cadBox.y0 << ".." << cadBox.y1 << ", +z overlay " << plusBox.x0 << ".."
                << plusBox.x1 << " x " << plusBox.y0 << ".." << plusBox.y1 << ", -z overlay " << minusBox.x0 << ".." << minusBox.x1
                << " x " << minusBox.y0 << ".." << minusBox.y1);
    constexpr int kSlack = 3;   // pixels: a one-pixel line over a silhouette edge
    CHECK(std::abs(plusBox.x0 - cadBox.x0) <= kSlack);
    CHECK(std::abs(plusBox.x1 - cadBox.x1) <= kSlack);
    CHECK(std::abs(plusBox.y0 - cadBox.y0) <= kSlack);
    CHECK(std::abs(plusBox.y1 - cadBox.y1) <= kSlack);
    // The -z overlay is lower on the screen than the solid: it hangs below the base the solid stands on.
    CHECK(minusBox.y1 > cadBox.y1 + 10);
    CHECK(minusBox.y0 > cadBox.y0 + 10);
    WARN(std::format("SI m -> OCCT mm on screen: CAD 1 x 2 x 3 mm box x {}..{} y {}..{}; overlay of 0.001 x 0.002 x 0.003 m x {}..{} y {}..{} (|d| <= {} px); the -0.003 m mirror y {}..{}",
                     cadBox.x0, cadBox.x1, cadBox.y0, cadBox.y1, plusBox.x0, plusBox.x1, plusBox.y0, plusBox.y1, kSlack,
                     minusBox.y0, minusBox.y1));
}
TEST_CASE("ThermalViz_AnOverlayInMetresCoincidesWithAFortyMillimetreSolid", "[thermal][viz][units][frame]") {
    // The same measurement at the scale of the thermal fixtures: the 40 x 30 x 20 mm steel block and its edges traced in
    // metres. A factor of 1000 in either direction cannot hide.
    VizStudy study;
    const geometry::Body* body = study.regenerator.body(study.feature);
    REQUIRE(body != nullptr);
    const renderer::ThermalOverlay edges = boxEdgesOverlay(0.040, 0.030, 0.020);
    renderer::Viewer viewer = makeViewer();
    const auto cad = viewer.display(study.feature, *body);
    REQUIRE(cad.has_value());
    const auto traced = viewer.displayOverlay(study.feature, edges);
    REQUIRE(traced.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();
    REQUIRE(viewer.setVisible(*traced, false).has_value());
    const Box cadBox = boundsOf(renderOf(viewer));
    REQUIRE(viewer.setVisible(*cad, false).has_value());
    REQUIRE(viewer.setVisible(*traced, true).has_value());
    const Box overlayBox = boundsOf(renderOf(viewer));
    REQUIRE(cadBox.pixels > 5000U);
    REQUIRE(overlayBox.pixels > 200U);
    CHECK(std::abs(overlayBox.x0 - cadBox.x0) <= 3);
    CHECK(std::abs(overlayBox.x1 - cadBox.x1) <= 3);
    CHECK(std::abs(overlayBox.y0 - cadBox.y0) <= 3);
    CHECK(std::abs(overlayBox.y1 - cadBox.y1) <= 3);
}

TEST_CASE("ThermalViz_DoesNotMutateMeshDuringUnitConversion", "[thermal][viz][units][readonly][frame]") {
    // Displaying converts a COPY. The mesh stays in metres, the view's buffer stays in metres, and showing the same view
    // twice gives the same image -- a conversion done in place would scale the mesh by 1000 each time.
    VizStudy study;
    const meshing::Mesh meshBefore = study.mesh();
    const Result<renderer::MeshView> built = renderer::MeshView::volumeBoundaryOf(study.mesh());
    REQUIRE(built.has_value());
    const std::vector<double> positionsBefore(built->positions().begin(), built->positions().end());
    REQUIRE(positionsBefore.size() > 0);
    CHECK(*std::ranges::max_element(positionsBefore) <= 0.040 + 1e-12);   // metres

    const auto imageOf = [&] {
        renderer::Viewer viewer = makeViewer();
        REQUIRE(viewer.displayMesh(study.feature, *built, renderer::MeshStyle::Shaded).has_value());
        REQUIRE(viewer.displayOverlay(study.feature, boxEdgesOverlay(0.040, 0.030, 0.020)).has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderOf(viewer);
    };
    const renderer::RenderedImage first = imageOf();
    const renderer::RenderedImage second = imageOf();
    CHECK(first == second);
    CHECK(study.mesh() == meshBefore);
    CHECK(std::vector<double>(built->positions().begin(), built->positions().end()) == positionsBefore);
}

// ===========================================================================
// The contour on screen, and the stale state
// ===========================================================================

TEST_CASE("ThermalViz_ATemperatureContourReachesTheScreenAsManyColours", "[thermal][viz][contour][frame]") {
    VizStudy study(20_mm, 6_mm);
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    renderer::ThermalContourCache cache;
    const auto colours = cache.colours(mesh, view, derived->map, *derived->post, 0, renderer::ThermalChannel::Temperature, 16);
    REQUIRE(colours.has_value());

    renderer::Viewer viewer = makeViewer();
    const Result<renderer::PresentationId> shown = viewer.displayMesh(study.feature, view, renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();
    const renderer::RenderedImage plain = renderOf(viewer);
    REQUIRE(viewer.setMeshContour(*shown, **colours).has_value());
    CHECK(viewer.meshHasContour(*shown).value());
    const renderer::RenderedImage contoured = renderOf(viewer);

    // The contour changes the picture without changing the silhouette, and draws many more colours than a plain shaded mesh.
    CHECK(contoured.coverage(kBlack) == plain.coverage(kBlack));
    CHECK_FALSE(contoured == plain);
    const std::size_t many = distinctColours(contoured);
    const std::size_t few = distinctColours(plain);
    INFO("distinct colours: plain " << few << ", contoured " << many);
    CHECK(many > few);
    CHECK(many > 12U);

    REQUIRE(viewer.clearMeshContour(*shown).has_value());
    CHECK(renderOf(viewer) == plain);
}

TEST_CASE("ThermalViz_StaleResultSuppressesTheContourAndMarksTheMeshStale", "[thermal][viz][state][stale][frame]") {
    VizStudy study(20_mm, 6_mm);
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    renderer::ThermalContourCache cache;
    renderer::Viewer viewer = makeViewer();
    const Result<renderer::PresentationId> shown = viewer.displayMesh(study.feature, view, renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    // R1 CURRENT: the contour is drawn.
    const renderer::ThermalDisplayState current = displayStateOf(study, derived.get());
    REQUIRE(renderer::thermalFieldAvailability(current) == renderer::ThermalFieldAvailability::Available);
    const auto colours = cache.colours(mesh, view, derived->map, *derived->post, 0, renderer::ThermalChannel::Temperature, 16);
    REQUIRE(colours.has_value());
    REQUIRE(viewer.setMeshContour(*shown, **colours).has_value());
    const renderer::RenderedImage live = renderOf(viewer);
    const std::size_t liveColours = distinctColours(live);
    REQUIRE(liveColours > 12U);

    // THE SOURCE MOVES (a conductivity edit; the mesh stays current): the state is stale, so the application does not draw
    // the old contour -- it clears it and marks the mesh stale. The decision is the core's, read through the display state.
    study.editConductivity(52.0);
    const renderer::ThermalDisplayState stale = displayStateOf(study, derived.get());
    REQUIRE(stale.result == renderer::ThermalResultState::Stale);
    REQUIRE(renderer::thermalFieldAvailability(stale) == renderer::ThermalFieldAvailability::StaleResult);
    REQUIRE(viewer.clearMeshContour(*shown).has_value());
    REQUIRE(viewer.setMeshStale(*shown, true).has_value());
    CHECK(viewer.meshIsStale(*shown).value());
    CHECK_FALSE(viewer.meshHasContour(*shown).value());
    const renderer::RenderedImage frozen = renderOf(viewer);
    CHECK_FALSE(frozen == live);                                   // not the current picture
    CHECK(distinctColours(frozen) < liveColours);                  // no longer a contour
    // And it is not the plain current mesh either: it LOOKS stale.
    REQUIRE(viewer.setMeshStale(*shown, false).has_value());
    const renderer::RenderedImage plainCurrent = renderOf(viewer);
    CHECK_FALSE(frozen == plainCurrent);
}

TEST_CASE("ThermalViz_TransientFramesDrawFromTheirOwnStates", "[thermal][viz][playback][frame]") {
    VizStudy study(20_mm, 6_mm);
    define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 0.3, 1.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    Result<renderer::ThermalPlayback> playback = renderer::ThermalPlayback::of(*derived->post);
    REQUIRE(playback.has_value());
    renderer::ThermalContourCache cache;
    // AN ELEMENT CHANNEL: a nodal one would be drawn on the exterior facets by their corner nodes, and a planar face is
    // two triangles whose corners are the box corners -- all prescribed, so the surface picture would never move. The heat
    // flux of the tetrahedra under the facets does, as the front advances from the hot cap.
    const renderer::ThermalDisplayRange shared{0.0, 600000.0};
    renderer::Viewer viewer = makeViewer();
    const auto shown = viewer.displayMesh(study.feature, view, renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    std::vector<renderer::RenderedImage> images;
    for (std::size_t f = 0; f < playback->frameCount(); ++f) {
        REQUIRE(playback->select(f).has_value());
        const auto colours = cache.colours(mesh, view, derived->map, *derived->post, playback->frame(), renderer::ThermalChannel::HeatFluxMagnitude, 16, shared);
        REQUIRE(colours.has_value());
        REQUIRE(viewer.setMeshContour(*shown, **colours).has_value());
        images.push_back(renderOf(viewer));
    }
    // Frame 0 is the initial state (uniform but for the held caps), and the front advances: successive frames differ.
    CHECK_FALSE(images[0] == images[2]);
    CHECK_FALSE(images[1] == images[3]);
    CHECK_FALSE(images[2] == images[4]);
    // Replaying a frame reproduces its image exactly: a frame is a state, not an animation.
    REQUIRE(playback->select(2).has_value());
    const auto again = cache.colours(mesh, view, derived->map, *derived->post, 2, renderer::ThermalChannel::HeatFluxMagnitude, 16, shared);
    REQUIRE(again.has_value());
    REQUIRE(viewer.setMeshContour(*shown, **again).has_value());
    CHECK(renderOf(viewer) == images[2]);
}

// ===========================================================================
// The overlays on screen
// ===========================================================================

TEST_CASE("ThermalViz_ThermalOverlaysReachTheScreenInTheirOwnColours", "[thermal][viz][overlay][frame]") {
    VizStudy study;
    const FaceName plusX = [&] {
        for (std::size_t k = 0; k < 4; ++k) {
            const auto facets = meshing::boundaryFacetsOf(study.map(), study.side(k));
            const auto n = renderer::outwardNormalOf(study.mesh(), facets->facets.front());
            if (n->x > 0.99) {
                return study.side(k);
            }
        }
        return study.side(0);
    }();
    define(study, steadyDefinition(study, {surfaceLoad(1, plusX, 1000.0)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const meshing::Mesh& mesh = study.mesh();
    const Result<renderer::MeshView> boundary = renderer::MeshView::volumeBoundaryOf(mesh);
    REQUIRE(boundary.has_value());
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const auto fluxes = renderer::appliedHeatFluxGlyphs(*boundary, mesh, study.map(), def.loads, *derived->loads);
    const auto held = renderer::prescribedTemperatureGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    const auto convecting = renderer::convectionGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    const auto vectorGlyphs = renderer::heatFluxVectorGlyphs(mesh, *derived->post, 0);
    REQUIRE(fluxes.has_value());
    REQUIRE(held.has_value());
    REQUIRE(convecting.has_value());
    REQUIRE(vectorGlyphs.has_value());

    const auto vectors = renderer::heatFluxVectorOverlay(*vectorGlyphs, 0.010, derived->post->states()[0].fields.heatFluxMagnitudes[0].si() * 4.0);
    const auto applied = renderer::appliedHeatFluxOverlay(mesh, *fluxes, 0.012, 1000.0);
    const auto temperature = renderer::prescribedTemperatureOverlay(mesh, *held, 0.008);
    const auto convection = renderer::convectionOverlay(mesh, *convecting, 0.008);
    REQUIRE(vectors.has_value());
    REQUIRE(applied.has_value());
    REQUIRE(temperature.has_value());
    REQUIRE(convection.has_value());

    struct Row {
        const renderer::ThermalOverlay* overlay;
        std::array<int, 3> expected;   // the role's colour, 8-bit sRGB
    };
    const std::array<Row, 4> rows{{{&*vectors, {191, 38, 191}}, {&*applied, {242, 64, 51}}, {&*temperature, {38, 89, 242}}, {&*convection, {26, 191, 217}}}};
    const geometry::Body* body = study.regenerator.body(study.feature);
    REQUIRE(body != nullptr);
    for (const Row& row : rows) {
        INFO(renderer::toString(row.overlay->role));
        renderer::Viewer viewer = makeViewer();
        REQUIRE(viewer.displayMesh(study.feature, *boundary, renderer::MeshStyle::Shaded).has_value());
        const Result<renderer::PresentationId> shown = viewer.displayOverlay(study.feature, *row.overlay);
        REQUIRE(shown.has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        const renderer::RenderedImage withOverlay = renderOf(viewer);
        REQUIRE(viewer.setVisible(*shown, false).has_value());
        const renderer::RenderedImage without = renderOf(viewer);
        CHECK_FALSE(withOverlay == without);
        // The overlay is drawn in ITS ROLE'S COLOUR somewhere in the picture.
        std::size_t inColour = 0;
        for (int y = 0; y < withOverlay.height; ++y) {
            for (int x = 0; x < withOverlay.width; ++x) {
                const renderer::Rgb p = withOverlay.at(x, y);
                if (std::abs(p.red - row.expected[0]) < 30 && std::abs(p.green - row.expected[1]) < 30 && std::abs(p.blue - row.expected[2]) < 30) {
                    ++inColour;
                }
            }
        }
        CHECK(inColour > 0U);
        // The kind says what it is, and a thermal overlay is not selectable: nothing is picked through it.
        bool found = false;
        for (const renderer::DisplayedObject& d : viewer.displayed()) {
            found = found || (d.presentation == *shown && d.kind == renderer::PresentationKind::ThermalOverlay);
        }
        CHECK(found);
    }
    // Nothing to draw is refused, not displayed as an empty presentation.
    renderer::ThermalOverlay empty;
    empty.role = renderer::ThermalOverlayRole::ConvectionSymbols;
    renderer::Viewer viewer = makeViewer();
    const auto refused = viewer.displayOverlay(study.feature, empty);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("convection_symbols"));
    CHECK_FALSE(viewer.displayOverlay(ObjectId{}, *vectors).has_value());
    CHECK(renderer::toString(renderer::PresentationKind::ThermalOverlay) == "a thermal overlay");
}

TEST_CASE("ThermalViz_ViewerStateChangesDoNotStaleThePhysics", "[thermal][viz][state][isolation][frame]") {
    // The camera, the visibility and the mesh style are the viewer's: none is a field of any source.
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const thermal::ThermalValidationReport report = validationOf(study, *derived);
    const renderer::ThermalDisplayState before = displayStateOf(study, derived.get(), &report);
    REQUIRE(before.result == renderer::ThermalResultState::Current);

    const Result<renderer::MeshView> view = renderer::MeshView::volumeBoundaryOf(study.mesh());
    REQUIRE(view.has_value());
    renderer::Viewer viewer = makeViewer();
    const auto shown = viewer.displayMesh(study.feature, *view, renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    for (const renderer::StandardView v : {renderer::StandardView::Front, renderer::StandardView::Top, renderer::StandardView::Isometric}) {
        viewer.setStandardView(v);
        viewer.fitAll();
    }
    viewer.orbit(Angle::fromSi(0.3), Angle::fromSi(0.2));
    viewer.pan(10.0, -5.0);
    viewer.zoom(1.5);
    REQUIRE(viewer.setMeshStyle(*shown, renderer::MeshStyle::Wireframe).has_value());
    REQUIRE(viewer.setVisible(*shown, false).has_value());
    REQUIRE(viewer.setVisible(*shown, true).has_value());
    REQUIRE(viewer.setSelected(*shown, true).has_value());
    viewer.clearSelection();
    (void)renderOf(viewer);

    CHECK(displayStateOf(study, derived.get(), &report) == before);
    CHECK(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
}

// ===========================================================================
// Picking resolves to engineering identity
// ===========================================================================

TEST_CASE("ThermalViz_PickMapsARenderTriangleToTheEngineeringElement", "[thermal][viz][pick][frame]") {
    VizStudy study(20_mm, 6_mm);
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    renderer::Viewer viewer = makeViewer();
    const auto shown = viewer.displayMesh(study.feature, view, renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    const Result<std::optional<renderer::MeshPick>> pick = viewer.pickMeshAt(kWidth / 2, kHeight / 2);
    REQUIRE(pick.has_value());
    REQUIRE(pick->has_value());
    CHECK((*pick)->presentation == *shown);
    // The pick is a RENDER TRIANGLE; the view translates it, and the translation is deterministic.
    const Result<meshing::ElementId> element = view.elementOfTriangle((*pick)->triangle);
    REQUIRE(element.has_value());
    CHECK(mesh.findTetrahedron(*element) != nullptr);   // a tetrahedron that owns the facet, not a render index
    const Result<renderer::ThermalElementInspection> inspected = renderer::inspectThermalElement(mesh, *derived->post, 0, *element, renderer::ThermalResultState::Current);
    REQUIRE(inspected.has_value());
    CHECK(inspected->element == *element);
    CHECK(inspected->ordinal == *derived->post->ordinalOf(*element));
    // The same pixel, picked again, resolves to the same element: no arbitrary choice among matches.
    for (int run = 0; run < 5; ++run) {
        const auto again = viewer.pickMeshAt(kWidth / 2, kHeight / 2);
        REQUIRE(again.has_value());
        REQUIRE(again->has_value());
        CHECK((*again)->triangle == (*pick)->triangle);
    }
    // A pixel outside the part hits nothing.
    const auto nothing = viewer.pickMeshAt(1, 1);
    REQUIRE(nothing.has_value());
    CHECK_FALSE(nothing->has_value());
    // A render VERTEX of the picked facet maps back to its engineering node, whose temperature the inspector reports through
    // the numbering -- never the render vertex index.
    const std::uint32_t corner = view.triangleIndices()[((*pick)->triangle * 3) + 0];
    const Result<meshing::NodeId> node = view.nodeOfVertex(corner);
    REQUIRE(node.has_value());
    CHECK(renderer::inspectThermalNode(mesh, derived->map, *derived->post, 0, *node, renderer::ThermalResultState::Current).has_value());
}

// ===========================================================================
// A larger mesh
// ===========================================================================

TEST_CASE("ThermalViz_LargeMeshVisualizationDataSmoke", "[thermal][viz][smoke]") {
    VizStudy study(20_mm, 2_mm);
    const FaceName hotSide = [&] {
        for (std::size_t k = 0; k < 4; ++k) {
            const auto facets = meshing::boundaryFacetsOf(study.map(), study.side(k));
            const auto n = renderer::outwardNormalOf(study.mesh(), facets->facets.front());
            if (n->x > 0.99) {
                return study.side(k);
            }
        }
        return study.side(0);
    }();
    define(study, steadyDefinition(study, {surfaceLoad(1, hotSide, 800.0), generationLoad(2, 5.0e4)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const std::size_t nodes = mesh.nodes().size();
    const std::size_t tets = mesh.tetrahedra().size();
    REQUIRE(tets > 500U);

    const renderer::MeshView byTet = byTetrahedron(mesh);
    const Result<renderer::MeshView> boundary = renderer::MeshView::volumeBoundaryOf(mesh);
    REQUIRE(boundary.has_value());
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(mesh, byTet, derived->map, *derived->post);
    REQUIRE(result.has_value());

    std::size_t fieldValues = 0;
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {
        const auto field = result->field(mesh, derived->map, *derived->post, 0, channel);
        REQUIRE(field.has_value());
        CHECK(field->complete());
        CHECK(field->values.size() == (channel == renderer::ThermalChannel::Temperature ? nodes : tets));   // not the largest id
        for (const double v : field->values) {
            REQUIRE(std::isfinite(v));
        }
        fieldValues += field->values.size();
        const auto legend = renderer::thermalContourLegend(*field, 32);
        REQUIRE(legend.has_value());
        const auto colours = renderer::thermalContourCornerColours(byTet, *field, *legend);
        REQUIRE(colours.has_value());
        CHECK(colours->size() == 9 * byTet.triangleCount());
        for (const double c : *colours) {
            REQUIRE(std::isfinite(c));
        }
        REQUIRE(renderer::thermalExtremeOf(*field, renderer::ThermalExtremeKind::Maximum).has_value());
    }
    const auto glyphs = renderer::heatFluxVectorGlyphs(mesh, *derived->post, 0);
    REQUIRE(glyphs.has_value());
    CHECK(glyphs->size() == tets);
    const auto vectors = renderer::heatFluxVectorOverlay(*glyphs, 0.001, derived->post->states()[0].fields.heatFluxMagnitudes[0].si() * 10.0, 0.0);
    REQUIRE(vectors.has_value());
    CHECK(vectors->glyphs + vectors->skipped + vectors->tooSmall + vectors->belowThreshold == tets);

    const thermal::ThermalAnalysisDefinition def = study.definition();
    const auto fluxes = renderer::appliedHeatFluxGlyphs(*boundary, mesh, study.map(), def.loads, *derived->loads);
    const auto held = renderer::prescribedTemperatureGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    const auto convecting = renderer::convectionGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(fluxes.has_value());
    REQUIRE(held.has_value());
    REQUIRE(convecting.has_value());
    const std::size_t classified = (*fluxes)[0].cardinality.classified + (*held)[0].cardinality.classified + (*convecting)[0].cardinality.classified;
    CHECK((*fluxes)[0].cardinality.complete());
    CHECK((*held)[0].cardinality.complete());
    CHECK((*convecting)[0].cardinality.complete());
    const auto arrows = renderer::appliedHeatFluxOverlay(mesh, *fluxes, 0.003, 800.0);
    const auto symbols = renderer::prescribedTemperatureOverlay(mesh, *held, 0.0015);
    const auto waves = renderer::convectionOverlay(mesh, *convecting, 0.0015);
    REQUIRE(arrows.has_value());
    REQUIRE(symbols.has_value());
    REQUIRE(waves.has_value());
    CHECK(arrows->glyphs == (*fluxes)[0].cardinality.classified);

    WARN(std::format("large-mesh visualization data: {} nodes, {} tetrahedra, {} exterior facets on the by-tetrahedron view; "
                     "field values {} (nodal {} + element 2 x {}); vector glyphs {}; boundary glyphs q'' {} + T {} + convection {} "
                     "(classified {}); every cardinality complete, every value finite",
                     nodes, tets, byTet.triangleCount(), fieldValues, nodes, tets, glyphs->size(), arrows->glyphs, symbols->glyphs,
                     waves->glyphs, classified));
}

TEST_CASE("ThermalViz_LargeCurvedMeshVisualizationDataSmoke", "[thermal][viz][smoke][curved]") {
    // A CYLINDER, whose curved wall subdivides: many exterior facets, so the surface buffers and the boundary glyph sets are
    // not the dozen triangles of a block.
    VizStudy study(3_mm, std::nullopt, true);
    define(study, steadyDefinition(study, {surfaceLoad(1, study.side(0), 800.0), generationLoad(2, 5.0e4)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const std::size_t nodes = mesh.nodes().size();
    const std::size_t tets = mesh.tetrahedra().size();
    const std::size_t surface = mesh.triangles().size();
    REQUIRE(tets > 1000U);
    REQUIRE(surface > 100U);

    const renderer::MeshView byTet = byTetrahedron(mesh);
    CHECK(byTet.triangleCount() == surface);   // one exterior facet of a tetrahedron per boundary triangle
    const Result<renderer::MeshView> boundary = renderer::MeshView::volumeBoundaryOf(mesh);
    REQUIRE(boundary.has_value());
    CHECK(boundary->triangleCount() == surface);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(mesh, byTet, derived->map, *derived->post);
    REQUIRE(result.has_value());
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {
        const auto field = result->field(mesh, derived->map, *derived->post, 0, channel);
        REQUIRE(field.has_value());
        CHECK(field->complete());
        const auto legend = renderer::thermalContourLegend(*field, 32);
        REQUIRE(legend.has_value());
        const auto colours = renderer::thermalContourCornerColours(byTet, *field, *legend);
        REQUIRE(colours.has_value());
        CHECK(colours->size() == 9 * surface);
        for (const double c : *colours) {
            REQUIRE(std::isfinite(c));
        }
    }
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const auto fluxes = renderer::appliedHeatFluxGlyphs(*boundary, mesh, study.map(), def.loads, *derived->loads);
    const auto held = renderer::prescribedTemperatureGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    const auto convecting = renderer::convectionGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(fluxes.has_value());
    REQUIRE(held.has_value());
    REQUIRE(convecting.has_value());
    CHECK((*fluxes)[0].cardinality.complete());
    CHECK((*held)[0].cardinality.complete());
    CHECK((*convecting)[0].cardinality.complete());
    // The wall and the two caps are the whole boundary: no facet is in two glyph sets and none is in none.
    CHECK((*fluxes)[0].facets.size() + (*held)[0].facets.size() + (*convecting)[0].facets.size() == surface);
    const auto arrows = renderer::appliedHeatFluxOverlay(mesh, *fluxes, 0.002, 800.0);
    const auto symbols = renderer::prescribedTemperatureOverlay(mesh, *held, 0.001);
    const auto waves = renderer::convectionOverlay(mesh, *convecting, 0.001);
    REQUIRE(arrows.has_value());
    REQUIRE(symbols.has_value());
    REQUIRE(waves.has_value());
    CHECK(arrows->glyphs == (*fluxes)[0].facets.size());
    CHECK(symbols->glyphs == (*held)[0].facets.size());
    CHECK(waves->glyphs == (*convecting)[0].facets.size());
    WARN(std::format("large curved mesh: {} nodes, {} tetrahedra, {} exterior facets; boundary glyphs q'' {} + T {} + convection {} = {} facets, "
                     "all complete; temperature buffer {}, gradient and flux buffers {} each",
                     nodes, tets, surface, arrows->glyphs, symbols->glyphs, waves->glyphs, arrows->glyphs + symbols->glyphs + waves->glyphs, nodes, tets));
}
TEST_CASE("ThermalViz_LargeMeshVisualizationReachesTheScreen", "[thermal][viz][smoke][frame]") {
    VizStudy study(20_mm, 2_mm);
    define(study, steadyDefinition(study, {generationLoad(1, 5.0e4)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    renderer::ThermalContourCache cache;
    const auto colours = cache.colours(mesh, view, derived->map, *derived->post, 0, renderer::ThermalChannel::HeatFluxMagnitude, 24);
    REQUIRE(colours.has_value());
    renderer::Viewer viewer = makeViewer();
    const auto shown = viewer.displayMesh(study.feature, view, renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    REQUIRE(viewer.setMeshContour(*shown, **colours).has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();
    const renderer::RenderedImage image = renderOf(viewer);
    CHECK(image.coverage(kBlack) > 5000U);
    CHECK(viewer.displayed().size() == 1);
}
