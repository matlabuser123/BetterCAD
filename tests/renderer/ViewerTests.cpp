// INFRA-VIEWER-001: the 3D viewer.
//
// HOW A VIEWER IS TESTED WITHOUT SCREENSHOTS. Every assertion here is about
// state or about coverage -- how many pixels differ from the background -- and
// none is about a pixel's exact value. Drivers differ, and a test that demanded
// byte-identical framebuffers would be testing the GPU.
//
// The decisive pattern, used throughout: render an empty scene, render the
// same view with something in it, and require the coverage to change. A render
// that returns success and produces the same image either way has drawn
// nothing.
//
// THE VIEWS HERE ARE OFFSCREEN, into a native window that is never mapped, so
// the suite shows nothing on screen and needs no display server. That it has
// to be a real window rather than an Aspect_NeutralWindow is the finding
// recorded in the implementation: OCCT calls SetPixelFormat on the window's
// device context.
#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/renderer/Viewer.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using renderer::DisplayedObject;
using renderer::PresentationId;
using renderer::RenderedImage;
using renderer::Rgb;
using renderer::StandardView;
using renderer::Viewer;

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 256;
constexpr Rgb kBlack{0, 0, 0};

/// A block, and the body the document regenerated for it.
struct Block {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};

    Block(Length a = 30_mm, Length b = 20_mm, Length c = 10_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, a, b);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    /// The regenerator's own body, which is what every other consumer of a
    /// regenerated solid uses.
    [[nodiscard]] geometry::Body body() const {
        const geometry::Body* found = regenerator.body(feature);
        REQUIRE(found != nullptr);
        REQUIRE_FALSE(found->isEmpty());
        return *found;
    }
};

/// Two solids in one document, side by side, so a pick has something to get
/// WRONG. With a single presentation, a pickAt that ignored what it detected
/// and returned the first displayed object would pass every assertion.
struct TwoBlocks {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId left{};
    ObjectId right{};

    TwoBlocks() {
        left = add(0_mm);
        right = add(60_mm);
        requireReport(regenerator, document);
    }

    [[nodiscard]] geometry::Body bodyOf(ObjectId feature) const {
        const geometry::Body* found = regenerator.body(feature);
        REQUIRE(found != nullptr);
        REQUIRE_FALSE(found->isEmpty());
        return *found;
    }

private:
    [[nodiscard]] ObjectId add(Length x) {
        auto sketch = std::make_unique<sketch::Sketch>(
            x.si() == 0.0 ? "Left" : "Right", Frame3D::xy());
        (void)addRectangle(*sketch, x, 0_mm, 20_mm, 20_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            x.si() == 0.0 ? "LeftSolid" : "RightSolid",
            {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
        REQUIRE(extrude.has_value());
        return require(document.addObject(std::move(*extrude)));
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

[[nodiscard]] RenderedImage renderOf(Viewer& viewer) {
    Result<RenderedImage> image = viewer.render();
    if (!image.has_value()) {
        FAIL("the view could not be rendered: " << image.error().message);
    }
    return *image;
}

} // namespace

// ---------------------------------------------------------------------------
// The view itself
// ---------------------------------------------------------------------------

TEST_CASE("Viewer_CreatesAnOffscreenViewOnThisToolchain", "[renderer][viewer]") {
    // THE GATING TEST. Before INFRA-VIEWER-001 the dependency carried no
    // TKOpenGl, so no graphic driver existed and this call was impossible.
    Result<Viewer> viewer = Viewer::createOffscreen(kWidth, kHeight);
    if (!viewer.has_value()) {
        FAIL("no offscreen view: " << viewer.error().message);
    }
    CHECK(viewer->width() == kWidth);
    CHECK(viewer->height() == kHeight);
    CHECK(viewer->displayed().empty());
    CHECK(viewer->selectedObjects().empty());
}

TEST_CASE("Viewer_RefusesAViewWithNoArea", "[renderer][viewer][validation]") {
    CHECK(errorCode(Viewer::createOffscreen(0, 256)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(Viewer::createOffscreen(256, -1)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(Viewer::createForWindow(0, 256, 256)) == ErrorCode::InvalidArgument);
}

TEST_CASE("Viewer_AnEmptySceneCoversNothing", "[renderer][viewer][render]") {
    // The baseline every other coverage assertion is measured against. An
    // empty scene that covered pixels would make "something was drawn"
    // unprovable.
    Viewer viewer = makeViewer();
    const RenderedImage image = renderOf(viewer);
    CHECK(image.width == kWidth);
    CHECK(image.height == kHeight);
    CHECK(image.rgb.size() == static_cast<std::size_t>(kWidth) * kHeight * 3U);
    CHECK(image.coverage(kBlack) == 0);
}

// ---------------------------------------------------------------------------
// Displaying a CAD body
// ---------------------------------------------------------------------------

TEST_CASE("Viewer_DisplaysACadBodyAndDrawsIt", "[renderer][viewer][display]") {
    Block block;
    Viewer viewer = makeViewer();

    const Result<PresentationId> presentation = viewer.display(block.feature, block.body());
    REQUIRE(presentation.has_value());
    CHECK(presentation->isValid());
    viewer.setStandardView(StandardView::Isometric);

    const RenderedImage image = renderOf(viewer);
    // DREW SOMETHING, and that is what the comparison with the empty scene
    // establishes. A render that returned success and covered nothing would
    // pass a weaker test.
    const std::size_t covered = image.coverage(kBlack);
    INFO("covered " << covered << " of " << (kWidth * kHeight) << " pixels");
    CHECK(covered > 0);
    // Sanity on the other side: a fitted box should not fill the whole view.
    CHECK(covered < static_cast<std::size_t>(kWidth) * kHeight);

    REQUIRE(viewer.displayed().size() == 1);
    CHECK(viewer.displayed().front().object == block.feature);
    CHECK(viewer.displayed().front().presentation == *presentation);
    CHECK(viewer.displayed().front().visible);
    CHECK_FALSE(viewer.displayed().front().selected);
}

TEST_CASE("Viewer_RefusesToDisplayAnEmptyBodyOrAnInvalidObject",
          "[renderer][viewer][validation]") {
    Block block;
    Viewer viewer = makeViewer();

    CHECK(errorCode(viewer.display(ObjectId{}, block.body())) == ErrorCode::InvalidArgument);
    const Result<PresentationId> empty = viewer.display(block.feature, geometry::Body{});
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(empty.error().message, ContainsSubstring("no body to display"));
    CHECK(viewer.displayed().empty());
}

TEST_CASE("Viewer_RemovingAPresentationLeavesTheModelAlone", "[renderer][viewer][display]") {
    Block block;
    Viewer viewer = makeViewer();
    const PresentationId presentation = require(viewer.display(block.feature, block.body()));
    viewer.setStandardView(StandardView::Isometric);
    REQUIRE(renderOf(viewer).coverage(kBlack) > 0);

    REQUIRE(viewer.remove(presentation).has_value());
    CHECK(viewer.displayed().empty());
    CHECK(renderOf(viewer).coverage(kBlack) == 0);

    // The body is still the document's, and still displayable.
    CHECK_FALSE(block.body().isEmpty());
    CHECK(viewer.display(block.feature, block.body()).has_value());

    // And removing something that is not displayed is a refusal, not a no-op.
    CHECK(errorCode(viewer.remove(PresentationId::fromValue(999))) == ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Visibility: presentation state, never model state
// ---------------------------------------------------------------------------

TEST_CASE("Viewer_HidingIsNotDeleting", "[renderer][viewer][visibility]") {
    Block block;
    Viewer viewer = makeViewer();
    const PresentationId presentation = require(viewer.display(block.feature, block.body()));
    viewer.setStandardView(StandardView::Isometric);
    const std::size_t shown = renderOf(viewer).coverage(kBlack);
    REQUIRE(shown > 0);

    REQUIRE(viewer.setVisible(presentation, false).has_value());
    CHECK(renderOf(viewer).coverage(kBlack) == 0);
    // STILL THERE. The presentation is in the viewer, the body is in the
    // document, and nothing was destroyed to stop drawing it.
    REQUIRE(viewer.displayed().size() == 1);
    CHECK_FALSE(viewer.displayed().front().visible);
    CHECK(viewer.displayed().front().object == block.feature);
    CHECK_FALSE(block.body().isEmpty());

    REQUIRE(viewer.setVisible(presentation, true).has_value());
    CHECK(viewer.displayed().front().visible);
    // The same thing comes back, not something else.
    CHECK(renderOf(viewer).coverage(kBlack) == shown);
}

TEST_CASE("Viewer_HidingOneBodyLeavesTheOtherVisible", "[renderer][viewer][visibility]") {
    Block block;
    Viewer viewer = makeViewer();
    const PresentationId first = require(viewer.display(block.feature, block.body()));
    const PresentationId second = require(viewer.display(block.feature, block.body()));
    viewer.setStandardView(StandardView::Isometric);
    const std::size_t both = renderOf(viewer).coverage(kBlack);
    REQUIRE(both > 0);

    REQUIRE(viewer.setVisible(first, false).has_value());
    // The two presentations are the same geometry, so coverage is unchanged --
    // what must change is the STATE, and only for the one that was hidden.
    CHECK(renderOf(viewer).coverage(kBlack) == both);
    CHECK_FALSE(viewer.displayed()[0].visible);
    CHECK(viewer.displayed()[1].visible);
    CHECK(second.isValid());
}

// ---------------------------------------------------------------------------
// Selection resolves to CAD identity
// ---------------------------------------------------------------------------

TEST_CASE("Viewer_SelectionIsReportedAsCadIdentity", "[renderer][viewer][selection]") {
    Block block;
    Viewer viewer = makeViewer();
    const PresentationId presentation = require(viewer.display(block.feature, block.body()));

    CHECK(viewer.selectedObjects().empty());
    REQUIRE(viewer.setSelected(presentation, true).has_value());
    // OBJECTIDS, never presentation handles and never graphics indices: what a
    // caller does with a selection is a question about the MODEL.
    const std::vector<ObjectId> selected = viewer.selectedObjects();
    REQUIRE(selected.size() == 1);
    CHECK(selected.front() == block.feature);
    CHECK(viewer.displayed().front().selected);

    viewer.clearSelection();
    CHECK(viewer.selectedObjects().empty());
    CHECK_FALSE(viewer.displayed().front().selected);
}

TEST_CASE("Viewer_PickingAnEmptyViewFindsNothing", "[renderer][viewer][selection]") {
    Viewer viewer = makeViewer();
    const Result<std::optional<ObjectId>> picked = viewer.pickAt(kWidth / 2, kHeight / 2);
    REQUIRE(picked.has_value());
    CHECK_FALSE(picked->has_value());
}

TEST_CASE("Viewer_PickingADisplayedBodyResolvesToItsObject",
          "[renderer][viewer][selection]") {
    Block block;
    Viewer viewer = makeViewer();
    (void)require(viewer.display(block.feature, block.body()));
    viewer.setStandardView(StandardView::Top);
    REQUIRE(renderOf(viewer).coverage(kBlack) > 0);

    // The centre of a fitted top view is on the body.
    const Result<std::optional<ObjectId>> picked = viewer.pickAt(kWidth / 2, kHeight / 2);
    REQUIRE(picked.has_value());
    REQUIRE(picked->has_value());
    CHECK(**picked == block.feature);
}

TEST_CASE("Viewer_PickingDistinguishesTwoDisplayedBodies",
          "[renderer][viewer][selection]") {
    // THE TEST THAT CAN FAIL. Two solids 60 mm apart, viewed from the top: a
    // pick on the left one must resolve to the LEFT object, and a pick on the
    // right one to the right. With a single presentation displayed, a pickAt
    // that ignored what it detected would satisfy every other test here.
    TwoBlocks blocks;
    Viewer viewer = makeViewer();
    const PresentationId leftPresentation =
        require(viewer.display(blocks.left, blocks.bodyOf(blocks.left)));
    const PresentationId rightPresentation =
        require(viewer.display(blocks.right, blocks.bodyOf(blocks.right)));
    CHECK_FALSE(leftPresentation == rightPresentation);

    viewer.setStandardView(StandardView::Top);
    REQUIRE(renderOf(viewer).coverage(kBlack) > 0);

    // Scan a horizontal line and collect which object each hit resolves to.
    // Scanning rather than guessing two pixel positions: FitAll decides the
    // layout, so hard-coding coordinates would be asserting about the camera.
    std::vector<ObjectId> hits;
    for (int x = 0; x < kWidth; x += 2) {
        const Result<std::optional<ObjectId>> picked = viewer.pickAt(x, kHeight / 2);
        REQUIRE(picked.has_value());
        if (picked->has_value()) {
            hits.push_back(**picked);
        }
    }
    REQUIRE_FALSE(hits.empty());
    // BOTH objects are hit, and nothing else is.
    const bool sawLeft = std::ranges::find(hits, blocks.left) != hits.end();
    const bool sawRight = std::ranges::find(hits, blocks.right) != hits.end();
    INFO("hits " << hits.size() << ", left " << sawLeft << ", right " << sawRight);
    CHECK(sawLeft);
    CHECK(sawRight);
    for (const ObjectId hit : hits) {
        CHECK((hit == blocks.left || hit == blocks.right));
    }
    // And the left solid is hit to the LEFT of the right one: the pick follows
    // the geometry rather than the display order.
    const auto firstLeft = std::ranges::find(hits, blocks.left);
    const auto firstRight = std::ranges::find(hits, blocks.right);
    CHECK(firstLeft < firstRight);
}

TEST_CASE("Viewer_SelectionRefusesAPresentationItDoesNotHave",
          "[renderer][viewer][validation]") {
    Viewer viewer = makeViewer();
    CHECK(errorCode(viewer.setSelected(PresentationId::fromValue(7), true)) ==
          ErrorCode::NotFound);
    CHECK(errorCode(viewer.setVisible(PresentationId::fromValue(7), false)) ==
          ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

TEST_CASE("Viewer_StandardViewsChangeWhatIsDrawn", "[renderer][viewer][camera]") {
    // An asymmetric block, so the views cannot coincide: 30 x 20 x 10 mm
    // presents a different silhouette from the top, the front and the side.
    Block block;
    Viewer viewer = makeViewer();
    (void)require(viewer.display(block.feature, block.body()));

    std::vector<std::size_t> coverages;
    for (const StandardView view : {StandardView::Top, StandardView::Front, StandardView::Right}) {
        viewer.setStandardView(view);
        const std::size_t covered = renderOf(viewer).coverage(kBlack);
        INFO(renderer::toString(view) << " covered " << covered);
        CHECK(covered > 0);
        coverages.push_back(covered);
    }
    // FitAll normalises the apparent size, so the silhouettes differ in SHAPE
    // rather than necessarily in area. What must hold is that the images are
    // not all identical.
    viewer.setStandardView(StandardView::Top);
    const RenderedImage top = renderOf(viewer);
    viewer.setStandardView(StandardView::Front);
    const RenderedImage front = renderOf(viewer);
    CHECK(top.rgb != front.rgb);
}

TEST_CASE("Viewer_ZoomAndOrbitChangeTheImageWithoutTouchingTheModel",
          "[renderer][viewer][camera]") {
    Block block;
    Viewer viewer = makeViewer();
    (void)require(viewer.display(block.feature, block.body()));
    viewer.setStandardView(StandardView::Isometric);
    const RenderedImage before = renderOf(viewer);
    REQUIRE(before.coverage(kBlack) > 0);

    viewer.zoom(2.0);
    const RenderedImage zoomed = renderOf(viewer);
    CHECK(zoomed.rgb != before.rgb);

    viewer.setStandardView(StandardView::Isometric);
    viewer.orbit(Angle::fromSi(0.4), Angle::fromSi(0.2));
    const RenderedImage orbited = renderOf(viewer);
    CHECK(orbited.rgb != before.rgb);

    // The body is untouched by any of it.
    CHECK_FALSE(block.body().isEmpty());
    CHECK(viewer.displayed().size() == 1);
}

TEST_CASE("Viewer_IgnoresANonsensicalZoom", "[renderer][viewer][camera][validation]") {
    Block block;
    Viewer viewer = makeViewer();
    (void)require(viewer.display(block.feature, block.body()));
    viewer.setStandardView(StandardView::Isometric);
    const RenderedImage before = renderOf(viewer);

    // A zero, negative or non-finite factor is not a camera move. Ignored
    // rather than passed to the kernel, which would raise.
    viewer.zoom(0.0);
    viewer.zoom(-1.0);
    viewer.zoom(std::numeric_limits<double>::quiet_NaN());
    CHECK(renderOf(viewer).rgb == before.rgb);
}

TEST_CASE("Viewer_ManyViewersInOneProcessEachRenderCorrectly",
          "[renderer][viewer][lifetime]") {
    // DOES A DESTROYED VIEWER GIVE ITS NATIVE RESOURCES BACK? Each viewer owns
    // an OpenGL context and a Win32 window, and both are process-wide
    // resources. If destruction does not release them, a long session leaks
    // one of each per view, and the symptom is not a leak message -- it is
    // RENDERS THAT QUIETLY GO WRONG once the process runs short.
    //
    // That is also why this is asserted rather than assumed: a leak here would
    // show up first as other tests in this very suite failing intermittently,
    // which is the hardest kind of failure to attribute.
    Block block;
    for (int iteration = 0; iteration < 30; ++iteration) {
        INFO("iteration " << iteration);
        Viewer viewer = makeViewer();
        const PresentationId presentation =
            require(viewer.display(block.feature, block.body()));
        viewer.setStandardView(StandardView::Isometric);
        viewer.fitAll();

        // The same two assertions the single-viewer tests make. If the
        // iteration number is what decides whether they hold, resources are
        // not being returned.
        const RenderedImage drawn = renderOf(viewer);
        REQUIRE(drawn.width == kWidth);
        REQUIRE(drawn.height == kHeight);
        const std::size_t covered = drawn.coverage(kBlack);
        REQUIRE(covered > 0);
        REQUIRE(covered < static_cast<std::size_t>(kWidth) * kHeight);

        REQUIRE(viewer.setVisible(presentation, false).has_value());
        REQUIRE(renderOf(viewer).coverage(kBlack) == 0);
    }
}

TEST_CASE("Viewer_PickCoordinatesStayInTheRenderedImagesSpaceAfterAResize",
          "[renderer][viewer][selection]") {
    // WHOSE PIXELS ARE A PICK'S PIXELS? A pick is normalised by OCCT against
    // the VIEW'S WINDOW, while render() produces an image of whatever size was
    // last asked for. Those are two sizes, and nothing in the API says they
    // are the same one -- so after a resize, a caller scanning the image it was
    // given could be picking in a different space from the one it is looking
    // at, and every pick would be displaced.
    //
    // This is the question the Qt widget's logical-versus-device defect is a
    // special case of, so it is asked of the renderer directly rather than
    // argued about: resize, render, and require the picks to still land on the
    // bodies the image shows.
    TwoBlocks blocks;
    Viewer viewer = makeViewer();
    (void)require(viewer.display(blocks.left, blocks.bodyOf(blocks.left)));
    (void)require(viewer.display(blocks.right, blocks.bodyOf(blocks.right)));
    viewer.setStandardView(StandardView::Top);

    // Deliberately not square, and not the size the view was created at: a
    // square resize would hide an aspect-ratio mistake, and the original size
    // would hide the whole question.
    constexpr int kWider = 400;
    constexpr int kShorter = 200;
    REQUIRE(viewer.resize(kWider, kShorter).has_value());
    viewer.fitAll();

    const RenderedImage image = renderOf(viewer);
    REQUIRE(image.width == kWider);
    REQUIRE(image.height == kShorter);
    REQUIRE(image.coverage(kBlack) > 0);

    // Scan the image's own middle row, in the image's own coordinates.
    std::vector<ObjectId> hits;
    for (int x = 0; x < image.width; x += 2) {
        const Result<std::optional<ObjectId>> picked = viewer.pickAt(x, image.height / 2);
        REQUIRE(picked.has_value());
        if (picked->has_value()) {
            hits.push_back(**picked);
        }
    }
    const bool sawLeft = std::ranges::find(hits, blocks.left) != hits.end();
    const bool sawRight = std::ranges::find(hits, blocks.right) != hits.end();
    INFO("after resize to " << kWider << "x" << kShorter << ": hits " << hits.size()
                            << ", left " << sawLeft << ", right " << sawRight);
    CHECK(sawLeft);
    CHECK(sawRight);
    for (const ObjectId hit : hits) {
        CHECK((hit == blocks.left || hit == blocks.right));
    }
    CHECK(std::ranges::find(hits, blocks.left) < std::ranges::find(hits, blocks.right));
}

TEST_CASE("Viewer_ResizeRefusesAnEmptyArea", "[renderer][viewer][validation]") {
    Viewer viewer = makeViewer();
    CHECK(errorCode(viewer.resize(0, 10)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(viewer.resize(10, 0)) == ErrorCode::InvalidArgument);
    REQUIRE(viewer.resize(128, 64).has_value());
    CHECK(viewer.width() == 128);
    CHECK(viewer.height() == 64);
    const RenderedImage image = renderOf(viewer);
    CHECK(image.width == 128);
    CHECK(image.height == 64);
}

// ---------------------------------------------------------------------------
// The background, and the coverage measure itself
// ---------------------------------------------------------------------------

TEST_CASE("Viewer_BackgroundIsWhatWasAskedFor", "[renderer][viewer][render]") {
    Viewer viewer = makeViewer();
    const Rgb grey{64, 72, 80};
    viewer.setBackground(grey);
    CHECK(viewer.background() == grey);

    const RenderedImage image = renderOf(viewer);
    // Measured against the colour that was SET, so an empty scene covers
    // nothing whatever the background is.
    CHECK(image.coverage(grey) == 0);
    // And it is really that colour, not black.
    CHECK(image.coverage(kBlack) ==
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));

    const Rgb sampled = image.at(0, 0);
    INFO("sampled (" << int{sampled.red} << "," << int{sampled.green} << "," << int{sampled.blue}
                     << ")");
    CHECK(std::abs(int{sampled.red} - int{grey.red}) <= 6);
    CHECK(std::abs(int{sampled.green} - int{grey.green}) <= 6);
    CHECK(std::abs(int{sampled.blue} - int{grey.blue}) <= 6);
}

TEST_CASE("RenderedImage_CoverageAndSamplingAreWellBehavedAtTheEdges",
          "[renderer][viewer][render]") {
    // The measure every other assertion rests on, tested on its own rather
    // than only through the viewer.
    RenderedImage image;
    image.width = 2;
    image.height = 2;
    // NO PIXEL IS BLACK, and that is the point. Out of bounds returns a
    // default-constructed Rgb -- which is black -- so a fixture with a black
    // corner cannot tell a refusing at() from one that CLAMPS to the nearest
    // pixel. A mutation that clamped survived this test until the fixture
    // stopped sharing a value with the sentinel.
    image.rgb = {10, 20, 30, 255, 255, 255, 40, 50, 60, 70, 80, 90};
    CHECK(image.at(0, 0) == Rgb{10, 20, 30});
    CHECK(image.at(1, 0) == Rgb{255, 255, 255});
    CHECK(image.at(0, 1) == Rgb{40, 50, 60});
    CHECK(image.at(1, 1) == Rgb{70, 80, 90});
    // Three of the four differ from the first pixel's colour by more than the
    // tolerance.
    CHECK(image.coverage(Rgb{10, 20, 30}) == 3);
    CHECK(image.coverage(Rgb{255, 255, 255}) == 3);
    // A tolerance wide enough to swallow every difference covers nothing.
    CHECK(image.coverage(Rgb{10, 20, 30}, 255) == 0);
    // Out of bounds is black rather than undefined behaviour, and black is a
    // colour no pixel here has.
    CHECK(image.at(-1, 0) == Rgb{});
    CHECK(image.at(0, 5) == Rgb{});
    CHECK(image.at(2, 2) == Rgb{});
    CHECK(image.at(0, -1) == Rgb{});
}

// ---------------------------------------------------------------------------
// Read-only: the viewer observes the model
// ---------------------------------------------------------------------------

TEST_CASE("Viewer_NothingItDoesChangesTheModel", "[renderer][viewer][readonly]") {
    // THE MANDATORY NON-MUTATION PROOF. A fingerprint of the canonical state
    // is taken before every kind of viewer operation and compared after: the
    // regenerated body, the document's revision of the feature, and the
    // kernel's own measure of the solid.
    //
    // The volume is the sharpest of the three. A viewer that moved a vertex,
    // healed a shape or re-triangulated anything would change it, and no
    // amount of correct bookkeeping elsewhere would hide that.
    Block block;
    const geometry::Body before = block.body();
    const Volume volumeBefore = require(before.massProperties()).volume;
    const auto revisionBefore = block.document.revisionOf(block.feature);

    Viewer viewer = makeViewer();
    const PresentationId presentation = require(viewer.display(block.feature, before));

    // Everything the viewer can be asked to do.
    viewer.setStandardView(StandardView::Isometric);
    viewer.fitAll();
    viewer.orbit(Angle::fromSi(0.3), Angle::fromSi(-0.2));
    viewer.pan(10.0, -5.0);
    viewer.zoom(1.5);
    REQUIRE(viewer.redraw().has_value());
    REQUIRE(viewer.render().has_value());
    REQUIRE(viewer.setSelected(presentation, true).has_value());
    (void)viewer.pickAt(kWidth / 2, kHeight / 2);
    viewer.clearSelection();
    REQUIRE(viewer.setVisible(presentation, false).has_value());
    REQUIRE(viewer.setVisible(presentation, true).has_value());
    viewer.setBackground(Rgb{10, 20, 30});
    REQUIRE(viewer.resize(128, 128).has_value());
    REQUIRE(viewer.render().has_value());
    REQUIRE(viewer.remove(presentation).has_value());

    const geometry::Body after = block.body();
    CHECK_FALSE(after.isEmpty());
    CHECK(require(after.massProperties()).volume.si() == volumeBefore.si());
    CHECK(block.document.revisionOf(block.feature) == revisionBefore);
}

TEST_CASE("Viewer_DisplayingDoesNotRegenerateOrHealTheBody",
          "[renderer][viewer][readonly]") {
    // A Body copy shares the underlying kernel shape, so the viewer holds a
    // handle to geometry it does not own. Displaying the SAME body in two
    // viewers must therefore leave it exactly as it was for both.
    Block block;
    const Volume volume = require(block.body().massProperties()).volume;

    Viewer first = makeViewer();
    Viewer second = makeViewer();
    (void)require(first.display(block.feature, block.body()));
    (void)require(second.display(block.feature, block.body()));
    first.setStandardView(StandardView::Isometric);
    second.setStandardView(StandardView::Top);
    REQUIRE(first.render().has_value());
    REQUIRE(second.render().has_value());

    CHECK(require(block.body().massProperties()).volume.si() == volume.si());
    // And the two viewers are independent: one's state is not the other's.
    CHECK(first.displayed().size() == 1);
    CHECK(second.displayed().size() == 1);
    CHECK_FALSE(first.displayed().front().presentation ==
                PresentationId::fromValue(0));
}

TEST_CASE("StandardView_EveryViewHasADistinctName", "[renderer][viewer]") {
    constexpr std::array views{StandardView::Front,  StandardView::Back,  StandardView::Left,
                               StandardView::Right,  StandardView::Top,   StandardView::Bottom,
                               StandardView::Isometric};
    std::vector<std::string_view> names;
    for (const StandardView view : views) {
        CHECK(renderer::toString(view) != "unknown_standard_view");
        names.push_back(renderer::toString(view));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(names.size() == views.size());
}
