#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/drawing/Section.hpp>
#include <bettercad/drawing/View.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using drawing::CuttingPlane;
using drawing::HatchSettings;
using drawing::SectionKind;
using drawing::StandardView;

// P14-VIEW-002: section geometry, against closed forms.
//
// WHERE THE EXPECTED NUMBERS COME FROM. Every fixture is made of boxes, so
// every cut face is a rectangle and every expected area is a product of two
// lengths computed here. A 100 x 60 x 40 block cut across its depth exposes a
// 100 x 40 face of area 4000 mm^2; a block with a 20 x 20 duct through it
// exposes that face less the duct. No expected value is produced by the
// sectioning routine under test.
namespace {

constexpr double kMm = 1e-9;
constexpr double kMm2 = 1e6; // SI square metres -> mm^2

/// A 100 x 60 x 40 block with its near-left-bottom corner at the origin.
geometry::Body block() {
    auto b = geometry::makeBox(Point3D{}, 100_mm, 60_mm, 40_mm);
    REQUIRE(b.has_value());
    return std::move(*b);
}

/// The same block with a 20 x 20 duct running right through it along Y.
geometry::Body blockWithDuct() {
    auto duct = geometry::makeBox(Point3D{40_mm, -10_mm, 10_mm}, 20_mm, 80_mm, 20_mm);
    REQUIRE(duct.has_value());
    auto cut = geometry::booleanOperation(geometry::BooleanOperation::Difference, block(), *duct);
    REQUIRE(cut.has_value());
    return std::move(*cut);
}

/// A plane across the block's depth at y, looking along +Y.
CuttingPlane acrossDepth(double y) {
    CuttingPlane plane;
    plane.origin = Point3D{0_mm, y * units::mm, 0_mm};
    plane.normal = Direction3D::unitY();
    plane.reference = Direction3D::unitX();
    return plane;
}

Frame3D basis(StandardView view) {
    auto b = drawing::basisOf(view);
    REQUIRE(b.has_value());
    return *b;
}

double areaMm2(const drawing::SectionGeometry& g) { return g.area.si() * kMm2; }

} // namespace

// --- The sign convention -------------------------------------------------------------

TEST_CASE("SectionGeometry_RemovesTheMaterialBetweenTheViewerAndThePlane",
          "[drawing][section][p14]") {
    // The convention, checked as geometry rather than as a flag. A Front view
    // looks from -Y, so a section on a plane at y = 30 must remove y < 30 and
    // keep y > 30. A Rear view looks from +Y and must keep the other half.
    const CuttingPlane plane = acrossDepth(30.0);

    const auto fromFront = drawing::cutBody(block(), plane, basis(StandardView::Front));
    const auto fromRear = drawing::cutBody(block(), plane, basis(StandardView::Rear));
    REQUIRE(fromFront.has_value());
    REQUIRE(fromRear.has_value());

    const auto frontBounds = fromFront->boundingBox();
    const auto rearBounds = fromRear->boundingBox();
    REQUIRE(frontBounds.has_value());
    REQUIRE(rearBounds.has_value());

    // Front keeps the far half: y from 30 to 60.
    CHECK_THAT(frontBounds->min.y.in(units::mm), WithinAbs(30.0, 1e-6));
    CHECK_THAT(frontBounds->max.y.in(units::mm), WithinAbs(60.0, 1e-6));
    // Rear keeps the near half: y from 0 to 30.
    CHECK_THAT(rearBounds->min.y.in(units::mm), WithinAbs(0.0, 1e-6));
    CHECK_THAT(rearBounds->max.y.in(units::mm), WithinAbs(30.0, 1e-6));
    // The two halves are different, which is the whole point.
    CHECK_FALSE(frontBounds->min.y.si() == rearBounds->min.y.si());
}

TEST_CASE("SectionGeometry_KeptSideFollowsTheViewAndNotTheStoredPlane",
          "[drawing][section][p14]") {
    auto frame = drawing::frameOf(acrossDepth(30.0));
    REQUIRE(frame.has_value());
    // Plane normal +Y. A Front view's normal is -Y, so they oppose and the
    // front (the +Y side) is kept. A Rear view's normal is +Y, so they agree
    // and the back is kept.
    CHECK(drawing::keptSide(*frame, basis(StandardView::Front)) == geometry::SplitKeep::Front);
    CHECK(drawing::keptSide(*frame, basis(StandardView::Rear)) == geometry::SplitKeep::Back);
}

// --- Closed-form section areas -------------------------------------------------------

TEST_CASE("SectionGeometry_ASolidBlockSectionsToOneRectangleOfKnownArea",
          "[drawing][section][p14]") {
    // The block is 100 wide (x) and 40 tall (z), so a cut across its depth
    // exposes 100 x 40 = 4000 mm^2, and a Front view shows it at true size.
    const auto g = drawing::sectionGeometry(block(), acrossDepth(30.0), basis(StandardView::Front),
                                            1.0, Point2D{200_mm, 150_mm}, HatchSettings{});
    REQUIRE(g.has_value());

    REQUIRE(g->loops.size() == 1);
    CHECK(g->loops.front().outer);
    CHECK(g->loops.front().points.size() == 4);
    CHECK_THAT(areaMm2(*g), WithinRel(4000.0, 1e-9));

    // And it is placed where it was asked for: centred on (200, 150), so
    // x spans 150..250 and y spans 130..170.
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const Point2D& p : g->loops.front().points) {
        minX = std::min(minX, p.x.in(units::mm));
        maxX = std::max(maxX, p.x.in(units::mm));
        minY = std::min(minY, p.y.in(units::mm));
        maxY = std::max(maxY, p.y.in(units::mm));
    }
    CHECK_THAT(minX, WithinAbs(150.0, kMm));
    CHECK_THAT(maxX, WithinAbs(250.0, kMm));
    CHECK_THAT(minY, WithinAbs(130.0, kMm));
    CHECK_THAT(maxY, WithinAbs(170.0, kMm));
}

TEST_CASE("SectionGeometry_ADuctThroughTheBlockBecomesAVoidInTheSection",
          "[drawing][section][p14]") {
    // 100 x 40 of material less a 20 x 20 duct = 4000 - 400 = 3600 mm^2, and
    // the duct must come back as an INNER loop rather than more material.
    const auto g = drawing::sectionGeometry(blockWithDuct(), acrossDepth(30.0),
                                            basis(StandardView::Front), 1.0,
                                            Point2D{200_mm, 150_mm}, HatchSettings{});
    REQUIRE(g.has_value());

    REQUIRE(g->loops.size() == 2);
    CHECK(g->loops[0].outer);        // sorted largest first
    CHECK_FALSE(g->loops[1].outer);  // the duct
    CHECK_THAT(areaMm2(*g), WithinRel(3600.0, 1e-9));

    // The void is 20 x 20 = 400 mm^2 in its own right.
    const double voidArea =
        std::abs(0.5 * drawing::twiceSignedArea(g->loops[1].points)) * kMm2;
    CHECK_THAT(voidArea, WithinRel(400.0, 1e-9));
}

TEST_CASE("SectionGeometry_ScaleAppliesToTheSectionExactlyOnce", "[drawing][section][p14]") {
    // At 1:2 the 100 x 40 face draws 50 x 20, so its area is a QUARTER:
    // 1000 mm^2, not a half. That is what catches a factor applied once to
    // each axis but squared into the area, or applied twice to one.
    struct Case {
        double factor;
        double area;
        double width;
    };
    for (const Case& c : {Case{1.0, 4000.0, 100.0}, Case{0.5, 1000.0, 50.0},
                          Case{2.0, 16000.0, 200.0}}) {
        INFO("factor " << c.factor);
        const auto g = drawing::sectionGeometry(block(), acrossDepth(30.0),
                                                basis(StandardView::Front), c.factor,
                                                Point2D{200_mm, 150_mm}, HatchSettings{});
        REQUIRE(g.has_value());
        CHECK_THAT(areaMm2(*g), WithinRel(c.area, 1e-9));

        double minX = 1e9, maxX = -1e9;
        for (const Point2D& p : g->loops.front().points) {
            minX = std::min(minX, p.x.in(units::mm));
            maxX = std::max(maxX, p.x.in(units::mm));
        }
        CHECK_THAT(maxX - minX, WithinAbs(c.width, kMm));
    }
}

TEST_CASE("SectionGeometry_CuttingElsewhereChangesTheSectionAsTheShapeSays",
          "[drawing][section][p14]") {
    // The duct runs from z = 10 to z = 30, so a plane that crosses it gives a
    // void and the area is reduced; the block is uniform along y, so every
    // plane across its depth gives the same answer -- which is itself worth
    // asserting, because a section that depended on WHERE along a uniform
    // extrusion it cut would be wrong.
    for (const double y : {10.0, 30.0, 50.0}) {
        INFO("cut at y = " << y);
        const auto g = drawing::sectionGeometry(blockWithDuct(), acrossDepth(y),
                                                basis(StandardView::Front), 1.0,
                                                Point2D{200_mm, 150_mm}, HatchSettings{});
        REQUIRE(g.has_value());
        CHECK_THAT(areaMm2(*g), WithinRel(3600.0, 1e-9));
        CHECK(g->loops.size() == 2);
    }
}

// --- Hatch ---------------------------------------------------------------------------

TEST_CASE("SectionGeometry_HatchLiesInsideTheMaterialAndNowhereElse",
          "[drawing][section][p14]") {
    const auto g = drawing::sectionGeometry(blockWithDuct(), acrossDepth(30.0),
                                            basis(StandardView::Front), 1.0,
                                            Point2D{200_mm, 150_mm}, HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE_FALSE(g->hatch.empty());

    const std::vector<Point2D>& outer = g->loops[0].points;
    const std::vector<Point2D>& hole = g->loops[1].points;

    for (const auto& [a, b] : g->hatch) {
        // Sample the middle of each hatch segment rather than its ends, which
        // sit on the boundary where containment is deliberately undefined.
        const Point2D mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
        INFO("hatch midpoint (" << mid.x.in(units::mm) << ", " << mid.y.in(units::mm) << ")");
        CHECK(drawing::contains(outer, mid));
        CHECK_FALSE(drawing::contains(hole, mid));
    }
}

TEST_CASE("SectionGeometry_HatchSpacingAndAngleAreHonoured", "[drawing][section][p14]") {
    // Halving the spacing must roughly double the number of lines. "Roughly"
    // because the count depends on where the first line lands, so the check
    // is a ratio with a tolerance rather than an exact count.
    HatchSettings coarse;
    coarse.spacing = 10_mm;
    HatchSettings fine;
    fine.spacing = 5_mm;

    const auto a = drawing::sectionGeometry(block(), acrossDepth(30.0), basis(StandardView::Front),
                                            1.0, Point2D{200_mm, 150_mm}, coarse);
    const auto b = drawing::sectionGeometry(block(), acrossDepth(30.0), basis(StandardView::Front),
                                            1.0, Point2D{200_mm, 150_mm}, fine);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE_FALSE(a->hatch.empty());
    CHECK(b->hatch.size() > a->hatch.size());
    const double ratio = static_cast<double>(b->hatch.size()) / static_cast<double>(a->hatch.size());
    CHECK_THAT(ratio, WithinAbs(2.0, 0.35));

    // Every line runs at the requested angle.
    HatchSettings flat;
    flat.angle = 0_deg;
    const auto level = drawing::sectionGeometry(block(), acrossDepth(30.0),
                                                basis(StandardView::Front), 1.0,
                                                Point2D{200_mm, 150_mm}, flat);
    REQUIRE(level.has_value());
    REQUIRE_FALSE(level->hatch.empty());
    for (const auto& [p, q] : level->hatch) {
        CHECK_THAT((q.y - p.y).in(units::mm), WithinAbs(0.0, kMm)); // horizontal
    }
}

TEST_CASE("SectionGeometry_HatchIsRefusedWithBadSettings", "[drawing][section][p14]") {
    HatchSettings bad;
    bad.spacing = 0_mm;
    const auto g = drawing::sectionGeometry(block(), acrossDepth(30.0), basis(StandardView::Front),
                                            1.0, Point2D{}, bad);
    REQUIRE_FALSE(g.has_value());
    CHECK(errorCode(g) == ErrorCode::InvalidArgument);
}

// --- Failure paths -------------------------------------------------------------------

TEST_CASE("SectionGeometry_APlaneThatMissesTheBodyFails", "[drawing][section][p14]") {
    // The block spans y 0..60; a plane at y = 200 never reaches it.
    const auto g = drawing::sectionGeometry(block(), acrossDepth(200.0), basis(StandardView::Front),
                                            1.0, Point2D{}, HatchSettings{});
    REQUIRE_FALSE(g.has_value());
    CHECK(errorCode(g) == ErrorCode::FailedPrecondition);
}

// --- Half sections -------------------------------------------------------------------

TEST_CASE("SectionGeometry_AHalfSectionCutsHalfTheMaterialAndLeavesTheRestWhole",
          "[drawing][section][p14]") {
    // The block's cut face is 100 x 40. Stopping the cut at the block's
    // mid-width leaves half of it, so the cut face is 50 x 40 = 2000 mm^2 --
    // exactly half the full section's 4000, which is the check that the half
    // is a half and not merely smaller.
    CuttingPlane plane = acrossDepth(30.0);
    plane.kind = SectionKind::Half;
    plane.splitAt = 50_mm;

    const auto g = drawing::sectionGeometry(block(), plane, basis(StandardView::Front), 1.0,
                                            Point2D{200_mm, 150_mm}, HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE(g->loops.size() == 1);
    CHECK(g->loops.front().points.size() == 4);
    CHECK_THAT(areaMm2(*g), WithinRel(2000.0, 1e-9));

    // And the model itself keeps the half that was not cut: the solid still
    // reaches y = 0 (where the uncut half is) as well as y = 60.
    const auto solid = drawing::cutBody(block(), plane, basis(StandardView::Front));
    REQUIRE(solid.has_value());
    const auto bounds = solid->boundingBox();
    REQUIRE(bounds.has_value());
    CHECK_THAT(bounds->min.y.in(units::mm), WithinAbs(0.0, 1e-6));
    CHECK_THAT(bounds->max.y.in(units::mm), WithinAbs(60.0, 1e-6));
}

TEST_CASE("SectionGeometry_AHalfSectionThatStopsAtTheFarEdgeIsTheFullSection",
          "[drawing][section][p14]") {
    // splitAt at the near edge means the whole width is cut, so the half
    // section must agree with the full one exactly. A boundary case that
    // catches an off-by-one in which half is removed.
    CuttingPlane half = acrossDepth(30.0);
    half.kind = SectionKind::Half;
    half.splitAt = 0_mm;

    const auto asHalf = drawing::sectionGeometry(block(), half, basis(StandardView::Front), 1.0,
                                                 Point2D{}, HatchSettings{});
    const auto asFull = drawing::sectionGeometry(block(), acrossDepth(30.0),
                                                 basis(StandardView::Front), 1.0, Point2D{},
                                                 HatchSettings{});
    REQUIRE(asHalf.has_value());
    REQUIRE(asFull.has_value());
    CHECK_THAT(areaMm2(*asHalf), WithinRel(areaMm2(*asFull), 1e-9));
    CHECK_THAT(areaMm2(*asHalf), WithinRel(4000.0, 1e-9));
}

TEST_CASE("SectionGeometry_AHalfSectionPastTheModelCutsNothingAndSaysSo",
          "[drawing][section][p14]") {
    // The block is 100 wide, so a cut that only starts at 150 removes nothing.
    // That is a section the engineer did not mean, not an empty drawing.
    CuttingPlane plane = acrossDepth(30.0);
    plane.kind = SectionKind::Half;
    plane.splitAt = 150_mm;

    const auto g = drawing::cutBody(block(), plane, basis(StandardView::Front));
    REQUIRE_FALSE(g.has_value());
    CHECK(errorCode(g) == ErrorCode::FailedPrecondition);
    CHECK_THAT(g.error().message, ContainsSubstring("removes nothing"));
}

TEST_CASE("SectionGeometry_AHalfSectionThroughADuctGivesANotchedOutline",
          "[drawing][section][p14]") {
    // Cutting from x = 50 crosses the duct at x 40..60, so the duct takes a
    // 10 x 20 bite out of the LEFT EDGE of the 50 x 40 cut face rather than
    // leaving a void in the middle: 2000 - 200 = 1800 mm^2, still one loop,
    // and eight corners rather than four.
    CuttingPlane plane = acrossDepth(30.0);
    plane.kind = SectionKind::Half;
    plane.splitAt = 50_mm;

    const auto g = drawing::sectionGeometry(blockWithDuct(), plane, basis(StandardView::Front), 1.0,
                                            Point2D{}, HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE(g->loops.size() == 1);
    CHECK(g->loops.front().outer);
    CHECK(g->loops.front().points.size() == 8);
    CHECK_THAT(areaMm2(*g), WithinRel(1800.0, 1e-9));
}

// --- Offset sections -----------------------------------------------------------------

TEST_CASE("SectionGeometry_LegFramesAreParallelAndOffsetAlongTheNormal",
          "[drawing][section][p14]") {
    CuttingPlane plane = acrossDepth(30.0);
    plane.kind = SectionKind::Offset;
    plane.legs.push_back({50_mm, 10_mm});
    plane.legs.push_back({70_mm, -5_mm});

    const auto frames = drawing::legFrames(plane);
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() == 3); // the base plane plus one per leg

    // Parallel: the same normal throughout. Displaced: each leg's origin sits
    // its own offset along that normal.
    for (const Frame3D& f : *frames) {
        CHECK(f.normal() == frames->front().normal());
        CHECK(f.xAxis() == frames->front().xAxis());
    }
    CHECK_THAT((*frames)[0].origin().y.in(units::mm), WithinAbs(30.0, kMm));
    CHECK_THAT((*frames)[1].origin().y.in(units::mm), WithinAbs(40.0, kMm));
    CHECK_THAT((*frames)[2].origin().y.in(units::mm), WithinAbs(25.0, kMm));
}

TEST_CASE("SectionGeometry_AnOffsetSectionDevelopsIntoOnePlaneWithNoJogLine",
          "[drawing][section][p14]") {
    // The block is uniform along its depth, so cutting the left half at
    // y = 30 and the right half at y = 40 exposes the same 100 x 40 face as a
    // single plane would: 4000 mm^2, in ONE loop. The jog between the legs
    // must leave no line and no seam, because the two legs are parallel and
    // the view looks along their shared normal -- which is why a stepped
    // section needs no separate development step.
    CuttingPlane plane = acrossDepth(30.0);
    plane.kind = SectionKind::Offset;
    plane.legs.push_back({50_mm, 10_mm});

    const auto g = drawing::sectionGeometry(block(), plane, basis(StandardView::Front), 1.0,
                                            Point2D{200_mm, 150_mm}, HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE(g->loops.size() == 1);
    CHECK(g->loops.front().points.size() == 4); // not 6, and not two rectangles
    CHECK_THAT(areaMm2(*g), WithinRel(4000.0, 1e-9));

    // The solid really did get cut at two different depths, even though the
    // section draws as one: the near face steps from y = 30 to y = 40.
    const auto solid = drawing::cutBody(block(), plane, basis(StandardView::Front));
    REQUIRE(solid.has_value());
    const auto bounds = solid->boundingBox();
    REQUIRE(bounds.has_value());
    CHECK_THAT(bounds->min.y.in(units::mm), WithinAbs(30.0, 1e-6));
    CHECK_THAT(bounds->max.y.in(units::mm), WithinAbs(60.0, 1e-6));
}

TEST_CASE("SectionGeometry_AnOffsetSectionCarriesAVoidAcrossItsJog",
          "[drawing][section][p14]") {
    // The duct spans x 40..60 and the jog is at x = 50, so the void is cut on
    // the base plane on one side of the jog and on the leg's plane on the
    // other. Developed, the two halves must join into ONE 20 x 20 void:
    // 4000 - 400 = 3600 mm^2 in two loops, the same answer a single plane
    // gives, because the block is uniform along its depth.
    CuttingPlane plane = acrossDepth(30.0);
    plane.kind = SectionKind::Offset;
    plane.legs.push_back({50_mm, 10_mm});

    const auto g = drawing::sectionGeometry(blockWithDuct(), plane, basis(StandardView::Front), 1.0,
                                            Point2D{}, HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE(g->loops.size() == 2);
    CHECK(g->loops[0].outer);
    CHECK_FALSE(g->loops[1].outer);
    CHECK(g->loops[1].points.size() == 4); // one void, not two halves of one
    CHECK_THAT(areaMm2(*g), WithinRel(3600.0, 1e-9));

    // And nothing is hatched inside it.
    for (const auto& [a, b] : g->hatch) {
        const Point2D mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
        CHECK_FALSE(drawing::contains(g->loops[1].points, mid));
    }
}

TEST_CASE("SectionGeometry_AnOffsetSectionWithSeveralJogsStillDevelopsToOneFace",
          "[drawing][section][p14]") {
    // Three legs, stepping out and back. The block is uniform along its
    // depth, so however many times the path jogs the developed face is the
    // same 100 x 40.
    CuttingPlane plane = acrossDepth(30.0);
    plane.kind = SectionKind::Offset;
    plane.legs.push_back({30_mm, 10_mm});
    plane.legs.push_back({60_mm, -5_mm});
    plane.legs.push_back({80_mm, 5_mm});

    const auto g = drawing::sectionGeometry(block(), plane, basis(StandardView::Front), 1.0,
                                            Point2D{}, HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE(g->loops.size() == 1);
    CHECK_THAT(areaMm2(*g), WithinRel(4000.0, 1e-9));
}

TEST_CASE("SectionGeometry_AnOffsetSectionAgreesWithAFullSectionWhenItsLegsDoNotStep",
          "[drawing][section][p14]") {
    // Legs with no offset are a full section written the long way, so they
    // must give the identical answer. The check that the machinery for legs
    // adds nothing of its own.
    CuttingPlane offset = acrossDepth(30.0);
    offset.kind = SectionKind::Offset;
    offset.legs.push_back({50_mm, 0_mm});

    const auto stepped = drawing::sectionGeometry(blockWithDuct(), offset,
                                                  basis(StandardView::Front), 1.0, Point2D{},
                                                  HatchSettings{});
    const auto flat = drawing::sectionGeometry(blockWithDuct(), acrossDepth(30.0),
                                               basis(StandardView::Front), 1.0, Point2D{},
                                               HatchSettings{});
    REQUIRE(stepped.has_value());
    REQUIRE(flat.has_value());
    CHECK(stepped->loops.size() == flat->loops.size());
    CHECK_THAT(areaMm2(*stepped), WithinRel(areaMm2(*flat), 1e-9));
    CHECK_THAT(areaMm2(*stepped), WithinRel(3600.0, 1e-9));
}

// --- Determinism ---------------------------------------------------------------------

TEST_CASE("SectionGeometry_TheSameSectionIsIdenticalEveryTime",
          "[drawing][section][p14][determinism]") {
    const auto a = drawing::sectionGeometry(blockWithDuct(), acrossDepth(30.0),
                                            basis(StandardView::Front), 1.0,
                                            Point2D{200_mm, 150_mm}, HatchSettings{});
    const auto b = drawing::sectionGeometry(blockWithDuct(), acrossDepth(30.0),
                                            basis(StandardView::Front), 1.0,
                                            Point2D{200_mm, 150_mm}, HatchSettings{});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    // Loop count, order and every point, bit for bit. The loops are sorted
    // into a canonical order precisely so this can be asserted: the kernel's
    // own edge order carries no meaning.
    REQUIRE(a->loops.size() == b->loops.size());
    for (std::size_t i = 0; i < a->loops.size(); ++i) {
        INFO("loop " << i);
        CHECK(a->loops[i].outer == b->loops[i].outer);
        REQUIRE(a->loops[i].points.size() == b->loops[i].points.size());
        for (std::size_t j = 0; j < a->loops[i].points.size(); ++j) {
            CHECK(a->loops[i].points[j].x.si() == b->loops[i].points[j].x.si());
            CHECK(a->loops[i].points[j].y.si() == b->loops[i].points[j].y.si());
        }
    }
    CHECK(a->area.si() == b->area.si());
    REQUIRE(a->hatch.size() == b->hatch.size());
    for (std::size_t i = 0; i < a->hatch.size(); ++i) {
        CHECK(a->hatch[i].first.x.si() == b->hatch[i].first.x.si());
        CHECK(a->hatch[i].second.y.si() == b->hatch[i].second.y.si());
    }
}

TEST_CASE("SectionGeometry_TheLargestLoopComesFirstWhateverTheKernelsOrder",
          "[drawing][section][p14][determinism]") {
    // Canonical ordering, asserted directly: the material before its voids.
    const auto g = drawing::sectionGeometry(blockWithDuct(), acrossDepth(30.0),
                                            basis(StandardView::Front), 1.0, Point2D{},
                                            HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE(g->loops.size() == 2);
    const double first = std::abs(drawing::twiceSignedArea(g->loops[0].points));
    const double second = std::abs(drawing::twiceSignedArea(g->loops[1].points));
    CHECK(first > second);
}

// --- What the review found ------------------------------------------------------------

TEST_CASE("SectionGeometry_ACurvedCutEdgeIsRefusedRatherThanDropped",
          "[drawing][section][p14]") {
    // The defect this closes: a curved cut edge used to be skipped. A round
    // hole through a sectioned wall would then vanish from the outline -- the
    // wall's own rectangle still closes into a loop, the hole is not there to
    // be a void, and the section comes back looking correct with hatch drawn
    // straight across material that is not there.
    //
    // A cylinder cut across its axis has a circular cut edge, which is the
    // smallest case that reaches it.
    auto cylinder = geometry::makeCylinder(20_mm, 80_mm);
    REQUIRE(cylinder.has_value());

    CuttingPlane across;
    across.origin = Point3D{0_mm, 0_mm, 40_mm};
    across.normal = Direction3D::unitZ();
    across.reference = Direction3D::unitX();

    const auto g = drawing::sectionGeometry(*cylinder, across, basis(StandardView::Top), 1.0,
                                            Point2D{}, HatchSettings{});
    REQUIRE_FALSE(g.has_value());
    CHECK(errorCode(g) == ErrorCode::FailedPrecondition);
    CHECK_THAT(g.error().message, ContainsSubstring("P14-HLR-001"));
}

TEST_CASE("SectionGeometry_APlaneEdgeOnToItsViewIsRefusedWithThatReason",
          "[drawing][section][p14]") {
    // A plane at right angles to the direction of sight has no side between
    // it and the viewer. The old symptom was "the cut edges did not close
    // into any loop", which is true and explains nothing.
    CuttingPlane plane = acrossDepth(30.0);       // normal +Y
    const auto g = drawing::sectionGeometry(block(), plane, basis(StandardView::Top), 1.0,
                                            Point2D{}, HatchSettings{});
    REQUIRE_FALSE(g.has_value());
    CHECK(errorCode(g) == ErrorCode::FailedPrecondition);
    CHECK_THAT(g.error().message, ContainsSubstring("edge-on"));
}

TEST_CASE("SectionGeometry_LoopsStartAtTheirLowestCornerAndWindByWhatTheyAre",
          "[drawing][section][p14][determinism]") {
    // Where a chained loop starts, and which way round it runs, were
    // accidents of the kernel's edge order: free to differ between builds
    // while the shape stayed identical. Both are now fixed, which is what
    // lets the determinism test compare points at all -- and it leaves the
    // loops in the winding a non-zero fill rule already expects.
    const auto g = drawing::sectionGeometry(blockWithDuct(), acrossDepth(30.0),
                                            basis(StandardView::Front), 1.0,
                                            Point2D{200_mm, 150_mm}, HatchSettings{});
    REQUIRE(g.has_value());
    REQUIRE(g->loops.size() == 2);

    for (const drawing::SectionLoop& loop : g->loops) {
        INFO((loop.outer ? "the material" : "the void"));
        for (const Point2D& p : loop.points) {
            const bool lower = p.x.si() < loop.points.front().x.si() ||
                               (p.x.si() == loop.points.front().x.si() &&
                                p.y.si() < loop.points.front().y.si());
            CHECK_FALSE(lower);
        }
        // Material counter-clockwise, voids clockwise.
        CHECK((drawing::twiceSignedArea(loop.points) > 0.0) == loop.outer);
    }
}
