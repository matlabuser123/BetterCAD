#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using drawing::CuttingPlane;
using drawing::DetailRegion;
using drawing::DrawingScale;
using drawing::DrawnEdge;
using drawing::HiddenLineSettings;
using drawing::ProjectionConvention;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::TangentEdgePolicy;
using drawing::ViewDefinition;
using drawing::ViewKind;
using geometry::EdgeVisibility;
using geometry::ProjectedEdgeKind;

// P14-HLR-001: what a VIEW does with the lines hidden-line removal found.
//
// The geometry is validated in tests/core/geometry/HiddenLineTests.cpp against
// solids whose silhouettes can be written down. What is tested here is the
// three decisions a drawing makes about that geometry -- merge, hidden,
// tangent -- and that they change what is DRAWN and nothing else.
namespace {

constexpr double kMm = 1e-6;

struct Fixture {
    Document document{"Drawing"};
    ObjectId part{};
    SheetId sheet{};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    void regenerate() { REQUIRE(regenerator.regenerateAll(document).has_value()); }
};

/// A 100 x 60 x 40 block. Its pocket, when it has one, is 20 x 20 and 10
/// deep, at x 40..60 and z 10..30, so it draws the rectangle (40,10)..(60,30)
/// in a front view whichever face it is cut into.
Fixture makeFixture(bool rearPocket = false) {
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(sketchId.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    f.part = require(f.document.addObject(std::move(*extrude)));

    if (rearPocket) {
        // The plane's normal is +Y so the cut runs into the REAR face, and
        // its X is +Z so its own Y comes out as +X: the rectangle below is
        // therefore written (z, x), giving model x 40..60 and z 10..30.
        auto pocket = std::make_unique<sketch::Sketch>(
            "Pocket", require(Frame3D::create(Point3D{0_mm, 50_mm, 0_mm}, Direction3D::unitY(),
                                              Direction3D::unitZ())));
        addRectangle(*pocket, 10_mm, 40_mm, 20_mm, 20_mm);
        const ObjectId pocketId = require(f.document.addObject(std::move(pocket)));
        auto cut = features::ExtrudeFeature::create(
            "PocketCut", {.profile = SketchId::fromValue(pocketId.value()),
                          .depth = 10_mm,
                          .operation = features::FeatureOperation::Cut,
                          .target = FeatureId::fromValue(f.part.value())});
        REQUIRE(cut.has_value());
        f.part = require(f.document.addObject(std::move(*cut)));
    }

    f.sheet = require(drawing::createSheet(
        f.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1},
                        .convention = ProjectionConvention::FirstAngle}));
    f.regenerate();
    return f;
}

ViewDefinition baseView(const Fixture& f, HiddenLineSettings settings) {
    return ViewDefinition{.sheet = f.sheet,
                          .source = ObjectReference{f.part},
                          .orientation = StandardView::Front,
                          .hiddenLine = settings,
                          .placement = Point2D{200_mm, 150_mm}};
}

/// How many drawn lines of the given visibility fall strictly inside the
/// block's outline -- which, for the fixtures here, means the pocket.
std::ptrdiff_t inside(const drawing::ProjectedGeometry& drawn, EdgeVisibility visibility) {
    return std::ranges::count_if(drawn.edges, [&](const DrawnEdge& e) {
        const double x = e.midpoint.x.in(units::mm);
        const double y = e.midpoint.y.in(units::mm);
        return e.visibility == visibility && x > 151.0 && x < 249.0 && y > 131.0 && y < 169.0;
    });
}

} // namespace

// --- Coincident lines -----------------------------------------------------------------

TEST_CASE("HiddenLineView_ABoxIsDrawnAsARectangleAndNotTwice", "[drawing][hlr][p14]") {
    // Without merging, every box in the system draws its outline twice: once
    // solid from its near face and once dashed from its far face, which lands
    // exactly on it. ISO 128 gives the rule -- a visible line takes
    // precedence over a hidden one -- and this is it, as a count.
    Fixture f = makeFixture();
    const ViewId id =
        require(drawing::createView(f.document, "Front", baseView(f, HiddenLineSettings{})));
    const auto drawn = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(drawn.has_value());

    CHECK(drawn->edges.size() == 4);
    CHECK(drawn->merged == 4);
    CHECK(drawn->suppressed == 0);
    // And the survivors are the visible ones, not the hidden twins.
    for (const DrawnEdge& e : drawn->edges) {
        CHECK(e.visibility == EdgeVisibility::Visible);
        CHECK(e.kind == ProjectedEdgeKind::Sharp);
    }

    // The rectangle really is the block's outline, centred where the view was
    // placed: 100 wide by 40 tall about (200, 150).
    CHECK_THAT(drawn->bounds.width().in(units::mm), WithinAbs(100.0, kMm));
    CHECK_THAT(drawn->bounds.height().in(units::mm), WithinAbs(40.0, kMm));
}

// --- The per-view toggle --------------------------------------------------------------

TEST_CASE("HiddenLineView_ARearPocketIsDrawnOnlyWhenHiddenLinesAreOn",
          "[drawing][hlr][p14]") {
    // The pocket is cut into the face AWAY from the viewer, so every line of
    // it is hidden. Turning hidden lines off must remove exactly those lines
    // and leave the outline alone.
    Fixture f = makeFixture(true);
    HiddenLineSettings on;
    on.showHidden = true;
    HiddenLineSettings off;
    off.showHidden = false;

    const ViewId shown = require(drawing::createView(f.document, "Shown", baseView(f, on)));
    const ViewId plain = require(drawing::createView(f.document, "Plain", baseView(f, off)));

    const auto withHidden = drawing::projectedGeometry(f.document, shown, f.bodies());
    const auto without = drawing::projectedGeometry(f.document, plain, f.bodies());
    REQUIRE(withHidden.has_value());
    REQUIRE(without.has_value());

    // With hidden lines on: the outline plus the pocket, the pocket hidden.
    CHECK(inside(*withHidden, EdgeVisibility::Hidden) == 4);
    CHECK(inside(*withHidden, EdgeVisibility::Visible) == 0);

    // With them off: the outline alone, and the pocket's four lines counted
    // as suppressed rather than quietly absent.
    CHECK(inside(*without, EdgeVisibility::Hidden) == 0);
    CHECK(inside(*without, EdgeVisibility::Visible) == 0);
    CHECK(without->suppressed == withHidden->edges.size() - without->edges.size());
    CHECK(without->suppressed == 4);
}

TEST_CASE("HiddenLineView_TheToggleChangesWhatIsDrawnAndNothingElse",
          "[drawing][hlr][p14]") {
    // The toggle is presentation. It must not move the view, resize it,
    // reorient it, or change what it draws -- only which of those lines
    // appear. Each of those is asserted rather than assumed, because a
    // drawing that shifted when hidden lines were turned off would be a
    // drawing whose dimensions moved with a display setting.
    Fixture f = makeFixture(true);
    HiddenLineSettings on;
    on.showHidden = true;
    HiddenLineSettings off;
    off.showHidden = false;

    const ViewId a = require(drawing::createView(f.document, "Shown", baseView(f, on)));
    const ViewId b = require(drawing::createView(f.document, "Plain", baseView(f, off)));

    CHECK(drawing::effectiveBasis(f.document, a)->normal() ==
          drawing::effectiveBasis(f.document, b)->normal());
    CHECK(*drawing::effectiveScale(f.document, a) == *drawing::effectiveScale(f.document, b));
    CHECK(drawing::effectivePlacement(f.document, a)->x.si() ==
          drawing::effectivePlacement(f.document, b)->x.si());
    CHECK(*drawing::effectiveSource(f.document, a) == *drawing::effectiveSource(f.document, b));

    const auto withHidden = drawing::projectedGeometry(f.document, a, f.bodies());
    const auto without = drawing::projectedGeometry(f.document, b, f.bodies());
    REQUIRE(withHidden.has_value());
    REQUIRE(without.has_value());

    // The bounds are taken from what the view COULD draw, so they do not move
    // when lines are suppressed. This is the assertion that catches a drawing
    // that re-centres itself when hidden detail is switched off.
    CHECK(withHidden->bounds.min == without->bounds.min);
    CHECK(withHidden->bounds.max == without->bounds.max);

    // And every line the plain view draws is drawn identically by the other.
    for (const DrawnEdge& e : without->edges) {
        CHECK(std::ranges::find(withHidden->edges, e) != withHidden->edges.end());
    }
}

// --- The tangent-edge policy ----------------------------------------------------------

TEST_CASE("HiddenLineView_TangentEdgesAreDrawnOnlyWhenThePolicySaysSo",
          "[drawing][hlr][p14]") {
    // A fillet runs into the face it blends with no crease. Whether the
    // drawing shows that line is a house rule, so it is a setting -- and it
    // must move only that line.
    Fixture f = makeFixture();
    auto fillet = features::FilletFeature::create(
        "Fillet", {.target = FeatureId::fromValue(f.part.value()),
                   .edges = {geometry::lineSignature(Point3D{100_mm, 0_mm, 0_mm},
                                                      Direction3D::unitZ())},
                   .radius = 8_mm});
    REQUIRE(fillet.has_value());
    f.part = require(f.document.addObject(std::move(*fillet)));
    f.regenerate();

    HiddenLineSettings show;
    show.tangentEdges = TangentEdgePolicy::Show;
    HiddenLineSettings hide;
    hide.tangentEdges = TangentEdgePolicy::Hide;

    const ViewId a = require(drawing::createView(f.document, "Shown", baseView(f, show)));
    const ViewId b = require(drawing::createView(f.document, "Plain", baseView(f, hide)));
    const auto withTangents = drawing::projectedGeometry(f.document, a, f.bodies());
    const auto without = drawing::projectedGeometry(f.document, b, f.bodies());
    REQUIRE(withTangents.has_value());
    REQUIRE(without.has_value());

    const auto smooth = [](const drawing::ProjectedGeometry& drawn) {
        return std::ranges::count_if(drawn.edges, [](const DrawnEdge& e) {
            return e.kind == ProjectedEdgeKind::Smooth;
        });
    };
    CHECK(smooth(*withTangents) >= 1);
    CHECK(smooth(*without) == 0);
    CHECK(without->edges.size() + static_cast<std::size_t>(smooth(*withTangents)) ==
          withTangents->edges.size());

    // The tangent line is where the fillet meets the front face, 8 mm in from
    // the block's right-hand end: 100 - 8 = 92, which the view centres at
    // 200, so x = 200 - 50 + 92 = 242.
    const bool atTheFillet = std::ranges::any_of(withTangents->edges, [](const DrawnEdge& e) {
        return e.kind == ProjectedEdgeKind::Smooth &&
               std::abs(e.start.x.in(units::mm) - 242.0) < 1e-3;
    });
    CHECK(atTheFillet);
}

// --- Advanced views -------------------------------------------------------------------

TEST_CASE("HiddenLineView_ASectionClassifiesTheCutSolidAndNotTheWholeOne",
          "[drawing][hlr][p14][section]") {
    // The order matters, and getting it wrong would be invisible in a
    // screenshot: a section that classified the UNCUT solid would hide the
    // very faces the section exists to show.
    //
    // The pocket is in the far face, so a plain front view hides all four of
    // its lines. Cutting through the pocket at y = 55 leaves a slab with a
    // hole in it, and the same four lines become visible -- nothing else
    // about the view changes.
    Fixture f = makeFixture(true);
    const ViewId front =
        require(drawing::createView(f.document, "Front", baseView(f, HiddenLineSettings{})));

    CuttingPlane plane;
    plane.origin = Point3D{0_mm, 55_mm, 0_mm};
    plane.normal = Direction3D::unitY().reversed(); // viewer on the -Y side, as Front is
    plane.reference = Direction3D::unitX();
    const ViewId section = require(drawing::createView(
        f.document, "SectionAA",
        ViewDefinition{.kind = ViewKind::Section,
                       .sheet = f.sheet,
                       .parent = front,
                       .section = plane,
                       .spacing = 80_mm}));

    const auto plain = drawing::projectedGeometry(f.document, front, f.bodies());
    const auto cut = drawing::projectedGeometry(f.document, section, f.bodies());
    REQUIRE(plain.has_value());
    REQUIRE(cut.has_value());

    // Uncut: the pocket is behind solid material.
    CHECK(inside(*plain, EdgeVisibility::Hidden) == 4);
    CHECK(inside(*plain, EdgeVisibility::Visible) == 0);

    // Cut through it: the same four lines, now seen. Counted on the section's
    // own placement rather than the front view's.
    const auto placement = drawing::effectivePlacement(f.document, section);
    REQUIRE(placement.has_value());
    const auto visibleInside = std::ranges::count_if(cut->edges, [&](const DrawnEdge& e) {
        const double x = (e.midpoint.x - placement->x).in(units::mm);
        const double y = (e.midpoint.y - placement->y).in(units::mm);
        return e.visibility == EdgeVisibility::Visible && std::abs(x) < 49.0 && std::abs(y) < 19.0;
    });
    CHECK(visibleInside == 4);
}

TEST_CASE("HiddenLineView_ADetailAppliesItsOwnSettingsRatherThanItsParents",
          "[drawing][hlr][p14][detail]") {
    // A detail is a view, so its hidden-line toggle is its own. If it could
    // only narrow what its parent had already filtered, the toggle would not
    // be per-view at all -- a detail of a plain view could never show hidden
    // detail, however it was set.
    Fixture f = makeFixture(true);
    HiddenLineSettings parentHidesThem;
    parentHidesThem.showHidden = false;
    const ViewId front =
        require(drawing::createView(f.document, "Front", baseView(f, parentHidesThem)));

    HiddenLineSettings detailShowsThem;
    detailShowsThem.showHidden = true;
    const ViewId detail = require(drawing::createView(
        f.document, "DetailB",
        ViewDefinition{.kind = ViewKind::Detail,
                       .sheet = f.sheet,
                       .parent = front,
                       .detail = DetailRegion{Point2D{200_mm, 150_mm}, 30_mm, true},
                       .hiddenLine = detailShowsThem,
                       .scale = DrawingScale{2, 1},
                       .placement = Point2D{320_mm, 100_mm}}));

    const auto parent = drawing::projectedGeometry(f.document, front, f.bodies());
    const auto drawn = drawing::projectedGeometry(f.document, detail, f.bodies());
    REQUIRE(parent.has_value());
    REQUIRE(drawn.has_value());

    // The parent shows none of the pocket.
    CHECK(std::ranges::none_of(parent->edges, [](const DrawnEdge& e) {
        return e.visibility == EdgeVisibility::Hidden;
    }));
    // The detail, over the middle of the same view, shows it.
    CHECK(std::ranges::any_of(drawn->edges, [](const DrawnEdge& e) {
        return e.visibility == EdgeVisibility::Hidden;
    }));
}

TEST_CASE("HiddenLineView_AnAuxiliaryViewClassifiesAlongItsOwnDirection",
          "[drawing][hlr][p14][auxiliary]") {
    // An auxiliary view looks a way no standard view does, so what it hides
    // is its own business. The check is that it hides SOMETHING and that its
    // outline is its own -- an auxiliary that quietly classified through its
    // parent's direction would draw the parent's hidden lines.
    Fixture f = makeFixture(true);
    const ViewId front =
        require(drawing::createView(f.document, "Front", baseView(f, HiddenLineSettings{})));

    const auto normal = Direction3D::fromComponents(1.0, -1.0, 0.0);
    REQUIRE(normal.has_value());
    const ViewId auxiliary = require(drawing::createView(
        f.document, "ViewC",
        ViewDefinition{.kind = ViewKind::Auxiliary,
                       .sheet = f.sheet,
                       .parent = front,
                       .auxiliary = drawing::ViewDirection{*normal, Direction3D::unitZ()},
                       .spacing = 120_mm}));

    const auto drawn = drawing::projectedGeometry(f.document, auxiliary, f.bodies());
    REQUIRE(drawn.has_value());
    REQUIRE_FALSE(drawn->edges.empty());

    // The view's right is its reference, +Z, along which the block is 40
    // tall; its up is normal x right = (-1,-1,0)/sqrt(2), along which the
    // block's 100 by 60 footprint measures (100 + 60) / sqrt(2) = 113.137.
    // Neither number belongs to any standard view of this block, which is
    // what says the auxiliary classified through its own basis.
    CHECK_THAT(drawn->bounds.width().in(units::mm), WithinAbs(40.0, 1e-3));
    CHECK_THAT(drawn->bounds.height().in(units::mm),
               WithinAbs((100.0 + 60.0) / std::numbers::sqrt2, 1e-3));
}

// --- Persistence ----------------------------------------------------------------------

TEST_CASE("HiddenLineView_SettingsRoundTripAndKeepDrawingTheSame",
          "[drawing][hlr][p14][persistence]") {
    Fixture f = makeFixture(true);
    HiddenLineSettings settings;
    settings.showHidden = false;
    settings.tangentEdges = TangentEdgePolicy::Show;
    const ViewId id = require(drawing::createView(f.document, "Front", baseView(f, settings)));
    const auto before = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(before.has_value());

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "hlr.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    const drawing::View* view = drawing::findView(*loaded, id);
    REQUIRE(view != nullptr);
    CHECK(view->definition().hiddenLine == settings);

    features::Regenerator reloaded;
    REQUIRE(reloaded.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &reloaded;
    const auto after =
        drawing::projectedGeometry(*loaded, id, [r](ObjectId object) { return r->body(object); });
    REQUIRE(after.has_value());

    REQUIRE(before->edges.size() == after->edges.size());
    for (std::size_t i = 0; i < before->edges.size(); ++i) {
        INFO("edge " << i);
        CHECK(before->edges[i] == after->edges[i]);
    }
    CHECK(before->merged == after->merged);
    CHECK(before->suppressed == after->suppressed);
}

TEST_CASE("HiddenLineView_AFileNamingAnUnknownTangentPolicyIsRefused",
          "[drawing][hlr][p14][persistence]") {
    Fixture f = makeFixture();
    (void)require(drawing::createView(f.document, "Front", baseView(f, HiddenLineSettings{})));
    const TempDir directory;
    const std::filesystem::path path = directory.path() / "bad.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());

    std::string text = readFile(path);
    const std::string from = R"("tangent_edges": "hide")";
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), R"("tangent_edges": "dashed")");
    writeFile(path, text);

    const auto loaded = io::loadDocument(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK_THAT(loaded.error().message, ContainsSubstring("unknown tangent-edge policy 'dashed'"));
}

// --- Determinism ----------------------------------------------------------------------

TEST_CASE("HiddenLineView_TheSameViewDrawsIdenticallyEveryTime",
          "[drawing][hlr][p14][determinism]") {
    Fixture f = makeFixture(true);
    HiddenLineSettings settings;
    settings.showHidden = true;
    const ViewId id = require(drawing::createView(f.document, "Front", baseView(f, settings)));

    const auto a = drawing::projectedGeometry(f.document, id, f.bodies());
    const auto b = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    REQUIRE(a->edges.size() == b->edges.size());
    for (std::size_t i = 0; i < a->edges.size(); ++i) {
        INFO("edge " << i);
        CHECK(a->edges[i] == b->edges[i]); // classification, order and every point
    }
    CHECK(a->merged == b->merged);
    CHECK(a->suppressed == b->suppressed);
}

// --- What the review found ------------------------------------------------------------

TEST_CASE("HiddenLineView_ACurveInsideADetailStaysOneLine", "[drawing][hlr][p14][detail]") {
    // The defect this closes: a piece that was not clipped used to have its
    // endpoint RECOMPUTED as a + 1.0 * (b - a), which is the same number in
    // arithmetic and not always the same double. Consecutive pieces then
    // failed to join, and a circle lying wholly inside the detail region came
    // back as a fan of two-point fragments instead of one line -- which draws
    // the same and is not the same thing at all, because every later
    // milestone that walks these edges would see dozens where there is one.
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    (void)require(sketch->addCircle(Point2D{0_mm, 0_mm}, 20_mm));
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Rod", {.profile = SketchId::fromValue(sketchId.value()), .depth = 80_mm});
    REQUIRE(extrude.has_value());
    f.part = require(f.document.addObject(std::move(*extrude)));
    f.sheet = require(drawing::createSheet(
        f.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1}}));
    f.regenerate();

    // Seen end-on the rod's rim IS a circle, so the parent draws one.
    const ViewId top = require(drawing::createView(
        f.document, "Top",
        ViewDefinition{.sheet = f.sheet,
                       .source = ObjectReference{f.part},
                       .orientation = StandardView::Top,
                       .placement = Point2D{200_mm, 150_mm}}));
    const auto parent = drawing::projectedGeometry(f.document, top, f.bodies());
    REQUIRE(parent.has_value());
    REQUIRE(parent->edges.size() == 1); // the near rim; the far one merges into it
    const std::size_t wholeCircle = parent->edges.front().polyline.size();
    CHECK(wholeCircle > 8);

    // A detail whose circle comfortably contains the rod keeps that one line.
    const ViewId detail = require(drawing::createView(
        f.document, "DetailB",
        ViewDefinition{.kind = ViewKind::Detail,
                       .sheet = f.sheet,
                       .parent = top,
                       .detail = DetailRegion{Point2D{200_mm, 150_mm}, 30_mm, true},
                       .scale = DrawingScale{2, 1},
                       .placement = Point2D{320_mm, 100_mm}}));
    const auto drawn = drawing::projectedGeometry(f.document, detail, f.bodies());
    REQUIRE(drawn.has_value());
    CHECK(drawn->edges.size() == 1);
    CHECK(drawn->edges.front().polyline.size() == wholeCircle);

    // And it really was enlarged: a 20 mm radius at 2:1 draws 40.
    CHECK_THAT(drawn->bounds.width().in(units::mm), WithinAbs(80.0, 0.05));
}

TEST_CASE("HiddenLineView_AViewOfSomethingThatProducesNoBodyDrawsNothingAtAll",
          "[drawing][hlr][p14]") {
    // Nothing is cached, so there is no previous result to leave standing:
    // a view whose source produces no body reports that, rather than handing
    // back the lines it drew last time.
    Fixture f = makeFixture();
    const ViewId id =
        require(drawing::createView(f.document, "Front", baseView(f, HiddenLineSettings{})));

    // Once with a body, so there would be something stale to return.
    REQUIRE(drawing::projectedGeometry(f.document, id, f.bodies()).has_value());

    // Then with nothing to draw.
    const auto drawn = drawing::projectedGeometry(f.document, id, [](ObjectId) {
        return static_cast<const geometry::Body*>(nullptr);
    });
    REQUIRE_FALSE(drawn.has_value());
    CHECK(errorCode(drawn) == ErrorCode::NotFound);
    CHECK_THAT(drawn.error().message, ContainsSubstring("produced no body"));

    // And the next good call is unaffected by the failed one.
    const auto again = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(again.has_value());
    CHECK(again->edges.size() == 4);
}
