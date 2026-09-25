#include "reference/DrawingTestSupport.hpp"

#include <bettercad/drawing/Section.hpp>
#include <bettercad/drawing/Sheets.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <utility>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// RM-DWG-03 as its builder sets it, restated here by hand.
constexpr double kBlockLength = 80.0;
constexpr double kBlockWidth = 60.0;
constexpr double kBlockHeight = 30.0;
constexpr double kPocketLength = 40.0;
constexpr double kPocketWidth = 30.0;
constexpr double kPocketDepth = 12.0;
constexpr double kStepLength = 20.0;
constexpr double kStepWidth = 15.0;
constexpr double kStepDepth = 8.0;

/// The block less both sinkings. Every one is a rectangular prism, so this is
/// exact -- no pi anywhere, and no tolerance needed beyond the kernel's own.
[[nodiscard]] constexpr double expectedVolumeMm3() {
    return kBlockLength * kBlockWidth * kBlockHeight - kPocketLength * kPocketWidth * kPocketDepth
           - kStepLength * kStepWidth * kStepDepth;
}
static_assert(expectedVolumeMm3() == 127200.0, "the arithmetic in the design note");

/// The face the cutting plane at y = 25 exposes: the block's cross-section
/// less the pocket's and the step's, all three rectangles.
constexpr double kExpectedCutAreaMm2 = kBlockLength * kBlockHeight
                                       - kPocketLength * kPocketDepth
                                       - kStepLength * kStepDepth;
static_assert(kExpectedCutAreaMm2 == 1760.0, "the arithmetic in the design note");

[[nodiscard]] reference::DrawnPocketBlockModel built() {
    auto model = reference::buildDrawnPocketBlockReferenceModel();
    INFO(why(model));
    REQUIRE(model.has_value());
    return std::move(*model);
}

} // namespace

TEST_CASE("DrawingReference_PocketBlockHasTheVolumeItsPocketsLeave",
          "[reference][drawing][rm-dwg-03]") {
    reference::DrawnPocketBlockModel m = built();
    Drawn d{std::move(m.document)};
    INFO(d.why());
    REQUIRE(d.succeeded());

    const geometry::Body* body = d.regenerator().body(m.step);
    REQUIRE(body != nullptr);
    CHECK_THAT(volumeMm3(*body), WithinRel(expectedVolumeMm3(), refmodel::kRel));

    CHECK_THAT(measuredMm(d, m.across), WithinAbs(kBlockLength, refmodel::kPositionMm));
    CHECK_THAT(measuredMm(d, m.tall), WithinAbs(kBlockHeight, refmodel::kPositionMm));
}

TEST_CASE("DrawingReference_PocketBlockSectionShowsWhatNoOutsideViewCan",
          "[reference][drawing][rm-dwg-03]") {
    // The whole point of a section: the cut passes through the pocket AND the
    // step in its floor, so the internal profile is drawn. An outside view of
    // this block shows a plain rectangle.
    reference::DrawnPocketBlockModel m = built();
    Drawn d{std::move(m.document)};
    REQUIRE(d.succeeded());

    const auto section = drawing::sectionOf(d.document(), m.section, d.bodies(), d.transforms());
    INFO(why(section));
    REQUIRE(section.has_value());
    // A cut face, and hatch drawn on it. Hatch with no cut face would be
    // hatch drawn across a void.
    CHECK_FALSE(section->loops.empty());
    CHECK_FALSE(section->hatch.empty());

    // THE CUT AREA, against a closed form. The plane at y = 25 passes through
    // the block, the pocket (y 10..40) and the step in its floor (y 15..30),
    // so the face it exposes is the block's 80 x 30 cross-section less the
    // pocket's 40 x 12 and the step's 20 x 8:
    //
    //     2400 - 480 - 160 = 1760 mm^2
    //
    // This is the assertion that a picture cannot fake. A section that missed
    // the step reads 1920; one that missed both reads 2400; one that hatched
    // across the voids reads 2400 as well. Every wrong section has a wrong
    // number here.
    CHECK_THAT(section->area.in(units::mm2), WithinRel(kExpectedCutAreaMm2, refmodel::kRel));

    // The section draws MORE than the plain outside view does: the pocket and
    // the step both appear in it. If the cut had missed them, or the internal
    // edges had been dropped, the two would have the same number of edges.
    const auto sectionEdges = drawing::projectedGeometry(d.document(), m.section, d.bodies(),
                                                         d.transforms());
    const auto frontEdges = drawing::projectedGeometry(d.document(), m.front, d.bodies(),
                                                       d.transforms());
    REQUIRE(sectionEdges.has_value());
    REQUIRE(frontEdges.has_value());
    INFO("section " << sectionEdges->edges.size() << " edges, front " << frontEdges->edges.size());
    CHECK(sectionEdges->edges.size() > 4);
}

TEST_CASE("DrawingReference_PocketBlockDetailIsDrawnAtTwiceSize",
          "[reference][drawing][rm-dwg-03]") {
    reference::DrawnPocketBlockModel m = built();
    Drawn d{std::move(m.document)};
    REQUIRE(d.succeeded());

    const auto scale = drawing::effectiveScale(d.document(), m.detail);
    INFO(why(scale));
    REQUIRE(scale.has_value());
    CHECK_THAT(scale->factor(), WithinAbs(2.0, 1e-15));

    // A detail view is a magnified crop, so it must draw something.
    const auto geometry = drawing::projectedGeometry(d.document(), m.detail, d.bodies(),
                                                     d.transforms());
    INFO(why(geometry));
    REQUIRE(geometry.has_value());
    CHECK_FALSE(geometry->edges.empty());

    // And the whole sheet -- three views at three different scales -- still
    // fits on the page.
    const drawing::DrawingScene scene = sceneOf(d, m.sheet);
    CHECK_FALSE(scene.items.lines.empty());
}
