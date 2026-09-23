#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/core/geometry/HiddenLine.hpp>
#include <bettercad/core/geometry/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using geometry::EdgeVisibility;
using geometry::ProjectedEdge;
using geometry::ProjectedEdgeKind;

// P14-HLR-001: hidden-line removal, against geometry worked out by hand.
//
// WHERE THE EXPECTED NUMBERS COME FROM. Every solid below is a primitive or a
// boolean of primitives whose silhouette and rims can be written down: a box
// draws its own extents, a sphere's silhouette is a circle of the sphere's
// radius, a cylinder seen across its axis is drawn by two generators r either
// side of its axis. No expected value is read back from the routine under
// test.
//
// TOLERANCES. Coordinates and lengths that come through the kernel's
// projection are compared to 1e-6 mm: hidden-line removal intersects surfaces
// and measures arc length numerically, which is geometric accumulation rather
// than the well-conditioned double arithmetic 1e-12 is for. Counts and
// classifications are exact.
namespace {

constexpr double kKernelMm = 1e-6;

/// The view that looks along +Y from -Y: x across, z up. Frame3D::xz() is
/// exactly this (ADR-013), which is why the drawing's x and y below are the
/// model's x and z.
Frame3D front() { return Frame3D::xz(); }

geometry::HiddenLineDrawing drawingOf(const geometry::Body& body, const Frame3D& basis) {
    auto drawing = geometry::hiddenLineDrawing(body, basis);
    REQUIRE(drawing.has_value());
    return std::move(*drawing);
}

geometry::HiddenLineDrawing drawingOf(std::span<const geometry::Body> bodies,
                                      const Frame3D& basis) {
    auto drawing = geometry::hiddenLineDrawing(bodies, basis);
    REQUIRE(drawing.has_value());
    return std::move(*drawing);
}

/// The edges of one member of a set, by the index it was given at.
std::vector<ProjectedEdge> edgesOf(const geometry::HiddenLineDrawing& drawing,
                                   std::size_t source) {
    std::vector<ProjectedEdge> mine;
    std::ranges::copy_if(drawing.edges, std::back_inserter(mine),
                         [&](const ProjectedEdge& e) { return e.source == source; });
    return mine;
}

/// How many edges of one member are of a visibility.
std::size_t countOf(const geometry::HiddenLineDrawing& drawing, std::size_t source,
                    EdgeVisibility visibility) {
    return static_cast<std::size_t>(
        std::ranges::count_if(drawing.edges, [&](const ProjectedEdge& e) {
            return e.source == source && e.visibility == visibility;
        }));
}

/// A 100 x 60 x 40 block, asymmetric in all three axes so a swapped or
/// mirrored projection changes the answer.
geometry::Body block() {
    auto body = geometry::makeBox(100_mm, 60_mm, 40_mm);
    REQUIRE(body.has_value());
    return std::move(*body);
}

/// The block with a 20 x 20 pocket 10 deep, in the face at @p y.
geometry::Body pocketed(Length y) {
    auto tool = geometry::makeBox(Point3D{40_mm, y, 10_mm}, 20_mm, 20_mm, 20_mm);
    REQUIRE(tool.has_value());
    auto cut = geometry::booleanOperation(geometry::BooleanOperation::Difference, block(), *tool);
    REQUIRE(cut.has_value());
    return std::move(*cut);
}

/// Whether any edge of the given visibility runs between the two points, in
/// either direction.
bool hasEdge(const geometry::HiddenLineDrawing& drawing, EdgeVisibility visibility,
             const Point2D& a, const Point2D& b) {
    return std::ranges::any_of(drawing.edges, [&](const ProjectedEdge& e) {
        const auto same = [](const Point2D& p, const Point2D& q) {
            return std::abs((p.x - q.x).in(units::mm)) < kKernelMm &&
                   std::abs((p.y - q.y).in(units::mm)) < kKernelMm;
        };
        return e.visibility == visibility &&
               ((same(e.start, a) && same(e.end, b)) || (same(e.start, b) && same(e.end, a)));
    });
}

/// Edges of one kind, in the drawing's canonical order.
std::vector<ProjectedEdge> ofKind(const geometry::HiddenLineDrawing& drawing,
                                  ProjectedEdgeKind kind) {
    std::vector<ProjectedEdge> found;
    std::ranges::copy_if(drawing.edges, std::back_inserter(found),
                         [&](const ProjectedEdge& e) { return e.kind == kind; });
    return found;
}

} // namespace

// --- The projection agrees with the qualified one -------------------------------------

TEST_CASE("HiddenLine_ProjectsIntoTheViewsOwnAxes", "[geometry][hlr][p14]") {
    // The kernel returns its answer already projected, in the coordinate
    // system of the projector it was given. That the projector was built so
    // this lands in the VIEW's axes is asserted here rather than trusted:
    // every drawn corner must be Frame3D::toLocal of the model corner, which
    // is the projection P0 qualified and P14-VIEW-001 draws with.
    const geometry::HiddenLineDrawing drawing = drawingOf(block(), front());

    // The block's eight corners project onto four drawn corners, because the
    // four running along the line of sight land on the four in front.
    for (const Point3D& corner : {Point3D{0_mm, 0_mm, 0_mm}, Point3D{100_mm, 0_mm, 0_mm},
                                  Point3D{100_mm, 0_mm, 40_mm}, Point3D{0_mm, 0_mm, 40_mm}}) {
        const Point2D expected = front().toLocal(corner);
        INFO("corner (" << corner.x.in(units::mm) << ", " << corner.z.in(units::mm) << ")");
        const bool drawn = std::ranges::any_of(drawing.edges, [&](const ProjectedEdge& e) {
            const auto at = [&](const Point2D& p) {
                return std::abs((p.x - expected.x).in(units::mm)) < kKernelMm &&
                       std::abs((p.y - expected.y).in(units::mm)) < kKernelMm;
            };
            return at(e.start) || at(e.end);
        });
        CHECK(drawn);
    }

    // And the hand-written numbers agree with toLocal, so neither is standing
    // in for the other.
    CHECK_THAT(front().toLocal(Point3D{100_mm, 0_mm, 40_mm}).x.in(units::mm), WithinAbs(100.0, 1e-12));
    CHECK_THAT(front().toLocal(Point3D{100_mm, 0_mm, 40_mm}).y.in(units::mm), WithinAbs(40.0, 1e-12));
}

// --- Case A: the box ------------------------------------------------------------------

TEST_CASE("HiddenLine_ABoxDrawsItsNearFaceVisibleAndItsFarFaceHidden",
          "[geometry][hlr][p14]") {
    // A box has 12 edges. Four run along the line of sight and project to
    // points, which is not a failure -- it is what an edge pointing at you
    // looks like -- so eight are drawn. The near four are the outline; the
    // far four land exactly on them and are hidden by the solid between.
    const geometry::HiddenLineDrawing drawing = drawingOf(block(), front());

    auto edges = geometry::listEdges(block());
    REQUIRE(edges.has_value());
    CHECK(edges->size() == 12); // the model really does have twelve

    CHECK(drawing.edges.size() == 8);
    CHECK(drawing.count(EdgeVisibility::Visible) == 4);
    CHECK(drawing.count(EdgeVisibility::Hidden) == 4);
    CHECK(drawing.count(ProjectedEdgeKind::Sharp) == 8);
    CHECK(drawing.count(ProjectedEdgeKind::Outline) == 0); // a box has no curved surface

    // The outline is the 100 x 40 rectangle, corner to corner.
    const Point2D bottomLeft{0_mm, 0_mm};
    const Point2D bottomRight{100_mm, 0_mm};
    const Point2D topRight{100_mm, 40_mm};
    const Point2D topLeft{0_mm, 40_mm};
    for (const auto& [a, b] : {std::pair{bottomLeft, bottomRight}, std::pair{bottomRight, topRight},
                               std::pair{topRight, topLeft}, std::pair{topLeft, bottomLeft}}) {
        CHECK(hasEdge(drawing, EdgeVisibility::Visible, a, b));
        CHECK(hasEdge(drawing, EdgeVisibility::Hidden, a, b)); // the far face, exactly behind
    }

    // Total drawn length: the rectangle twice over.
    double total = 0.0;
    for (const ProjectedEdge& e : drawing.edges) {
        total += e.length.in(units::mm);
    }
    CHECK_THAT(total, WithinAbs(2.0 * 2.0 * (100.0 + 40.0), kKernelMm));
}

// --- The depth convention, proved rather than assumed ---------------------------------

TEST_CASE("HiddenLine_APocketIsVisibleInTheNearFaceAndHiddenInTheFarOne",
          "[geometry][hlr][p14]") {
    // The whole depth convention in one pair of assertions. The same block
    // with the same pocket, once in the face toward the viewer and once in
    // the face away from it. If the sign were inverted the two answers would
    // simply swap, and every drawing BetterCAD produced would be inside out.
    //
    // The pocket draws the rectangle (40,10)..(60,30) either way; what
    // changes is whether it is visible.
    const Point2D a{40_mm, 10_mm};
    const Point2D b{60_mm, 10_mm};
    const Point2D c{60_mm, 30_mm};
    const Point2D d{40_mm, 30_mm};

    // Front view looks from -Y, so y = -10 cuts the NEAR face and y = 50 the far.
    const geometry::HiddenLineDrawing near = drawingOf(pocketed(-10_mm), front());
    const geometry::HiddenLineDrawing far = drawingOf(pocketed(50_mm), front());

    for (const auto& [from, to] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, d},
                                   std::pair{d, a}}) {
        CHECK(hasEdge(near, EdgeVisibility::Visible, from, to));
        CHECK_FALSE(hasEdge(far, EdgeVisibility::Visible, from, to));
        CHECK(hasEdge(far, EdgeVisibility::Hidden, from, to));
    }

    // Said again as a count, so the claim does not rest on the four segments
    // above being the only ones that matter: a pocket in the far face adds
    // NOTHING visible inside the outline.
    const auto insideOutline = [](const geometry::HiddenLineDrawing& drawing) {
        return std::ranges::count_if(drawing.edges, [](const ProjectedEdge& e) {
            const double x = e.midpoint.x.in(units::mm);
            const double y = e.midpoint.y.in(units::mm);
            return e.visibility == EdgeVisibility::Visible && x > 1.0 && x < 99.0 && y > 1.0 &&
                   y < 39.0;
        });
    };
    CHECK(insideOutline(near) == 4);
    CHECK(insideOutline(far) == 0);
}

// --- Case C: the cylinder, and silhouettes --------------------------------------------

TEST_CASE("HiddenLine_ACylinderIsDrawnByTwoSilhouetteGenerators", "[geometry][hlr][p14]") {
    // A cylinder seen across its axis has NO model edge along its length: the
    // two lines a drawing shows are silhouettes, the curves where the surface
    // turns away from the viewer. They stand r either side of the axis and
    // run the full height.
    //
    // Viewed from +X, because the surface's seam lies along +X and so faces
    // the viewer rather than landing on a silhouette -- see the side-view
    // case below for what happens when it does.
    auto cylinder = geometry::makeCylinder(20_mm, 80_mm);
    REQUIRE(cylinder.has_value());
    const geometry::HiddenLineDrawing drawing = drawingOf(*cylinder, Frame3D::yz());

    const std::vector<ProjectedEdge> outlines = ofKind(drawing, ProjectedEdgeKind::Outline);
    REQUIRE(outlines.size() == 2);
    for (const ProjectedEdge& e : outlines) {
        CHECK(e.visibility == EdgeVisibility::Visible);
        CHECK_THAT(e.length.in(units::mm), WithinAbs(80.0, kKernelMm));  // the full height
        CHECK_THAT(std::abs(e.start.x.in(units::mm)), WithinAbs(20.0, kKernelMm)); // r from the axis
        CHECK_THAT(e.start.x.si(), WithinAbs(e.end.x.si(), 1e-12));      // and vertical
    }
    // One each side, not two on one side.
    CHECK(outlines[0].start.x.si() < 0.0);
    CHECK(outlines[1].start.x.si() > 0.0);

    // The seam is reported as what it is -- an artefact of how the surface is
    // parameterised -- rather than as an edge of the shape.
    const std::vector<ProjectedEdge> seams = ofKind(drawing, ProjectedEdgeKind::Sewn);
    REQUIRE(seams.size() == 1);
    CHECK_THAT(seams.front().start.x.in(units::mm), WithinAbs(0.0, kKernelMm));
}

TEST_CASE("HiddenLine_ASeamLyingOnASilhouetteIsStillDrawnInTheRightPlace",
          "[geometry][hlr][p14]") {
    // The degenerate case, recorded because it is real and would otherwise
    // look like a missing silhouette. Seen from -Y the same cylinder's seam
    // lies exactly ON the right-hand silhouette, so the kernel has one curve
    // where the view from +X had two, and reports it as the model edge it is.
    //
    // What a DRAWING needs is unchanged and is what is asserted: two vertical
    // lines, r either side of the axis, each the full height.
    auto cylinder = geometry::makeCylinder(20_mm, 80_mm);
    REQUIRE(cylinder.has_value());
    const geometry::HiddenLineDrawing drawing = drawingOf(*cylinder, front());

    std::vector<double> generators;
    for (const ProjectedEdge& e : drawing.edges) {
        if (e.visibility == EdgeVisibility::Visible &&
            std::abs(e.length.in(units::mm) - 80.0) < kKernelMm) {
            generators.push_back(e.start.x.in(units::mm));
        }
    }
    std::ranges::sort(generators);
    REQUIRE(generators.size() == 2);
    CHECK_THAT(generators[0], WithinAbs(-20.0, kKernelMm));
    CHECK_THAT(generators[1], WithinAbs(20.0, kKernelMm));

    // The end rims are seen edge-on, so each draws as a segment of the full
    // diameter.
    CHECK(hasEdge(drawing, EdgeVisibility::Visible, Point2D{-20_mm, 0_mm}, Point2D{20_mm, 0_mm}));
    CHECK(hasEdge(drawing, EdgeVisibility::Visible, Point2D{-20_mm, 80_mm}, Point2D{20_mm, 80_mm}));
}

TEST_CASE("HiddenLine_ACylinderSeenAlongItsAxisDrawsItsRimsAsCircles",
          "[geometry][hlr][p14]") {
    // Seen end-on there is no silhouette generator at all: the outline IS the
    // rim. The near rim is visible, the far one is hidden behind it, and both
    // measure 2*pi*r.
    auto cylinder = geometry::makeCylinder(20_mm, 80_mm);
    REQUIRE(cylinder.has_value());
    const geometry::HiddenLineDrawing drawing = drawingOf(*cylinder, Frame3D::xy());

    CHECK(drawing.edges.size() == 2);
    CHECK(drawing.count(EdgeVisibility::Visible) == 1);
    CHECK(drawing.count(EdgeVisibility::Hidden) == 1);
    for (const ProjectedEdge& e : drawing.edges) {
        CHECK(e.curve == geometry::EdgeCurve::Circle);
        CHECK_THAT(e.length.in(units::mm), WithinRel(2.0 * std::numbers::pi * 20.0, 1e-9));
    }
}

TEST_CASE("HiddenLine_ASphereIsDrawnByACircleOfItsOwnRadius", "[geometry][hlr][p14]") {
    // A sphere has no edge at all that a drawing could show; everything drawn
    // is silhouette. Orthographically that silhouette is a great circle, so
    // it measures 2*pi*r and every point of it is exactly r from the centre.
    auto sphere = geometry::makeSphere(25_mm);
    REQUIRE(sphere.has_value());
    const geometry::HiddenLineDrawing drawing = drawingOf(*sphere, front());

    double total = 0.0;
    for (const ProjectedEdge& e : drawing.edges) {
        total += e.length.in(units::mm);
        for (const Point2D& p : {e.start, e.midpoint, e.end}) {
            CHECK_THAT(std::hypot(p.x.in(units::mm), p.y.in(units::mm)),
                       WithinAbs(25.0, kKernelMm));
        }
    }
    CHECK_THAT(total, WithinRel(2.0 * std::numbers::pi * 25.0, 1e-9));
    CHECK(drawing.count(EdgeVisibility::Hidden) == 0); // nothing of a sphere hides anything
}

// --- Case D: the stepped shaft --------------------------------------------------------

TEST_CASE("HiddenLine_ASteppedShaftDrawsAGeneratorPairPerStep", "[geometry][hlr][p14]") {
    // 20 radius for 40, then 10 radius for 30. Each step contributes two
    // generators r either side of the shared axis, running that step's own
    // height -- four vertical lines at x = -20, -10, +10, +20.
    auto big = geometry::makeCylinder(20_mm, 40_mm);
    auto small = geometry::makeCylinder(Axis3D{Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ()},
                                        10_mm, 30_mm);
    REQUIRE(big.has_value());
    REQUIRE(small.has_value());
    auto shaft = geometry::booleanOperation(geometry::BooleanOperation::Union, *big, *small);
    REQUIRE(shaft.has_value());
    const geometry::HiddenLineDrawing drawing = drawingOf(*shaft, front());

    struct Generator {
        double x;
        double length;
    };
    for (const Generator& g : {Generator{-20.0, 40.0}, Generator{20.0, 40.0},
                               Generator{-10.0, 30.0}, Generator{10.0, 30.0}}) {
        INFO("generator at x = " << g.x);
        CHECK(std::ranges::any_of(drawing.edges, [&](const ProjectedEdge& e) {
            return e.visibility == EdgeVisibility::Visible &&
                   std::abs(e.start.x.in(units::mm) - g.x) < kKernelMm &&
                   std::abs(e.end.x.in(units::mm) - g.x) < kKernelMm &&
                   std::abs(e.length.in(units::mm) - g.length) < kKernelMm;
        }));
    }

    // The shoulder: the big cylinder's top rim, seen edge-on across its full
    // diameter, and the small one's base rim hidden inside it.
    CHECK(hasEdge(drawing, EdgeVisibility::Visible, Point2D{-20_mm, 40_mm}, Point2D{20_mm, 40_mm}));
    CHECK(hasEdge(drawing, EdgeVisibility::Hidden, Point2D{-10_mm, 40_mm}, Point2D{10_mm, 40_mm}));
    // And the top.
    CHECK(hasEdge(drawing, EdgeVisibility::Visible, Point2D{-10_mm, 70_mm}, Point2D{10_mm, 70_mm}));
}

// --- Case E: tangent edges ------------------------------------------------------------

TEST_CASE("HiddenLine_AFilletsTangentEdgeIsClassifiedSmooth", "[geometry][hlr][p14]") {
    // A fillet runs into the face it blends with no crease, so the line where
    // they meet is real topology but is not a corner. Whether a drawing shows
    // it is policy, and policy needs the geometric fact told apart first.
    //
    // A 60 x 40 x 30 block filleted r = 8 on the vertical edge at (60, 0):
    // the fillet meets the front face along x = 60 - 8 = 52, for the block's
    // full height.
    auto body = geometry::makeBox(60_mm, 40_mm, 30_mm);
    REQUIRE(body.has_value());
    geometry::FilletRequest request;
    request.radius = 8_mm;
    request.edges.push_back(geometry::lineSignature(Point3D{60_mm, 0_mm, 0_mm}, Direction3D::unitZ()));
    auto filleted = geometry::filletEdges(*body, request);
    REQUIRE(filleted.has_value());

    // The model itself agrees the edge is tangent: its two faces meet at no
    // angle. That is the independent check that "smooth" means what it says.
    auto edges = geometry::listEdges(*filleted);
    REQUIRE(edges.has_value());
    const auto tangent = std::ranges::count_if(*edges, [](const geometry::EdgeInfo& e) {
        return e.faceAngle && std::abs(e.faceAngle->in(units::deg)) < 1e-6;
    });
    CHECK(tangent == 2); // one where the fillet meets each of the two faces

    const geometry::HiddenLineDrawing drawing = drawingOf(*filleted, front());
    const std::vector<ProjectedEdge> smooth = ofKind(drawing, ProjectedEdgeKind::Smooth);
    REQUIRE(smooth.size() == 1);
    CHECK(smooth.front().visibility == EdgeVisibility::Visible);
    CHECK_THAT(smooth.front().start.x.in(units::mm), WithinAbs(52.0, kKernelMm));
    CHECK_THAT(smooth.front().length.in(units::mm), WithinAbs(30.0, kKernelMm));

    // The front face is shortened to 52 by the fillet, and the fillet itself
    // draws as an 8 mm segment seen edge-on.
    CHECK(hasEdge(drawing, EdgeVisibility::Visible, Point2D{0_mm, 0_mm}, Point2D{52_mm, 0_mm}));
    CHECK(hasEdge(drawing, EdgeVisibility::Visible, Point2D{52_mm, 0_mm}, Point2D{60_mm, 0_mm}));
}

// --- Determinism ----------------------------------------------------------------------

TEST_CASE("HiddenLine_TheSameSolidAndDirectionDrawIdenticallyEveryTime",
          "[geometry][hlr][p14][determinism]") {
    // Bit for bit, and in the same order: the canonical sort exists precisely
    // so this can be asserted, because the kernel's own traversal order
    // carries no meaning.
    const geometry::Body body = pocketed(50_mm);
    const geometry::HiddenLineDrawing a = drawingOf(body, front());
    const geometry::HiddenLineDrawing b = drawingOf(body, front());

    REQUIRE(a.edges.size() == b.edges.size());
    for (std::size_t i = 0; i < a.edges.size(); ++i) {
        INFO("edge " << i);
        CHECK(a.edges[i] == b.edges[i]);
    }
}

TEST_CASE("HiddenLine_TheDrawingComesBackInItsCanonicalOrder",
          "[geometry][hlr][p14][determinism]") {
    // Kind, then visibility, then where it starts. Asserted directly, because
    // every comparison of two drawings depends on it.
    const geometry::HiddenLineDrawing drawing = drawingOf(pocketed(50_mm), front());
    REQUIRE(drawing.edges.size() > 1);
    for (std::size_t i = 1; i < drawing.edges.size(); ++i) {
        const ProjectedEdge& previous = drawing.edges[i - 1];
        const ProjectedEdge& current = drawing.edges[i];
        INFO("edge " << i);
        if (previous.kind != current.kind) {
            CHECK(previous.kind < current.kind);
            continue;
        }
        if (previous.visibility != current.visibility) {
            CHECK(previous.visibility < current.visibility);
            continue;
        }
        CHECK(previous.start.x.si() <= current.start.x.si());
    }
}

// --- Failure paths --------------------------------------------------------------------

TEST_CASE("HiddenLine_AnEmptyBodyHasNoDrawingAndSaysSo", "[geometry][hlr][p14]") {
    const auto drawing = geometry::hiddenLineDrawing(geometry::Body{}, front());
    REQUIRE_FALSE(drawing.has_value());
    CHECK(errorCode(drawing) == ErrorCode::FailedPrecondition);
    CHECK_THAT(drawing.error().message, ContainsSubstring("nothing to draw"));
}

// --- Curves are carried as curves ------------------------------------------------------

TEST_CASE("HiddenLine_AFullCircleIsSampledRatherThanLeftAsTwoCoincidentPoints",
          "[geometry][hlr][p14]") {
    // A full circle's start and end ARE the same point. A drawing that joined
    // them with a straight line would draw nothing at all, so the polyline is
    // what a renderer follows and the exact description is what a later
    // milestone measures.
    auto cylinder = geometry::makeCylinder(20_mm, 80_mm);
    REQUIRE(cylinder.has_value());
    const geometry::HiddenLineDrawing drawing = drawingOf(*cylinder, Frame3D::xy());
    REQUIRE(drawing.edges.size() == 2);

    for (const ProjectedEdge& e : drawing.edges) {
        REQUIRE(e.curve == geometry::EdgeCurve::Circle);
        // Start and end coincide, which is exactly why the polyline matters.
        CHECK_THAT((e.start.x - e.end.x).in(units::mm), WithinAbs(0.0, kKernelMm));
        CHECK_THAT((e.start.y - e.end.y).in(units::mm), WithinAbs(0.0, kKernelMm));

        // The sampling goes all the way round, staying on the circle.
        CHECK(e.polyline.size() > 8);
        for (const Point2D& p : e.polyline) {
            CHECK_THAT(std::hypot(p.x.in(units::mm), p.y.in(units::mm)),
                       WithinAbs(20.0, 0.01 + kKernelMm)); // within the sampling deflection
        }
        // And it reaches every side, so the bounds taken from it are the
        // circle's and not a chord's.
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const Point2D& p : e.polyline) {
            minX = std::min(minX, p.x.in(units::mm));
            maxX = std::max(maxX, p.x.in(units::mm));
            minY = std::min(minY, p.y.in(units::mm));
            maxY = std::max(maxY, p.y.in(units::mm));
        }
        CHECK_THAT(maxX - minX, WithinAbs(40.0, 0.02));
        CHECK_THAT(maxY - minY, WithinAbs(40.0, 0.02));
    }
}

TEST_CASE("HiddenLine_AStraightEdgeIsItsTwoEndpointsAndNoMore", "[geometry][hlr][p14]") {
    const geometry::HiddenLineDrawing drawing = drawingOf(block(), front());
    for (const ProjectedEdge& e : drawing.edges) {
        REQUIRE(e.curve == geometry::EdgeCurve::Line);
        CHECK(e.polyline.size() == 2);
        CHECK(e.polyline.front() == e.start);
        CHECK(e.polyline.back() == e.end);
    }
}


// --- Occlusion BETWEEN bodies (P14-ASM-001, closing P14-HLR-001's open item)
//
// Every fixture here is two boxes whose projected overlap is written down by
// hand. The front view looks along +Y from -Y (ADR-013: Frame3D::xz()'s normal
// is -Y), so SMALLER y is nearer the viewer and hides larger y.

TEST_CASE("HiddenLine_ABodyBehindAnotherIsHiddenWhereItIsCoveredAndVisibleWhereItIsNot",
          "[geometry][hiddenline][assembly][p14]") {
    // WHY THIS IS COMPARED AGAINST THE SAME BODY DRAWN ALONE. A box hides its
    // own back face, so every box has hidden edges whatever else is in the
    // scene -- "it has hidden edges" says nothing about occlusion BETWEEN
    // bodies. What does say something is that a line visible when the body is
    // drawn alone stops being visible when another body is put in front of
    // it.
    //
    // The front plate spans x in [0, 50] and z in [-10, 70]; the rear plate
    // spans x in [0, 100] and z in [0, 60]. So the front plate's shadow
    // covers the rear plate's left half OUTRIGHT, top and bottom edges
    // included, and reaches past it in z so that no two boundaries graze.
    auto front_ = geometry::makeBox(Point3D{0_mm, 0_mm, -10_mm}, 50_mm, 10_mm, 80_mm);
    auto rear = geometry::makeBox(Point3D{0_mm, 20_mm, 0_mm}, 100_mm, 10_mm, 60_mm);
    REQUIRE(front_.has_value());
    REQUIRE(rear.has_value());
    const std::vector<geometry::Body> bodies{*front_, *rear};

    const geometry::HiddenLineDrawing together = drawingOf(bodies, front());
    const geometry::HiddenLineDrawing rearAlone = drawingOf(*rear, front());

    // Drawn alone, the rear plate shows lines across its whole width.
    const auto leftmostVisible = [](const std::vector<ProjectedEdge>& edges) {
        double least = std::numeric_limits<double>::max();
        for (const ProjectedEdge& edge : edges) {
            if (edge.visibility == EdgeVisibility::Visible) {
                least = std::min({least, edge.start.x.in(units::mm), edge.end.x.in(units::mm)});
            }
        }
        return least;
    };
    CHECK_THAT(leftmostVisible(rearAlone.edges), WithinAbs(0.0, kKernelMm));

    // With the front plate there, nothing of the rear plate is visible left
    // of x = 50 -- which is the front plate's own right-hand edge.
    CHECK(countOf(together, 1, EdgeVisibility::Visible) > 0); // it is not simply gone
    CHECK(leftmostVisible(edgesOf(together, 1)) >= 50.0 - kKernelMm);

    // And the kernel SPLIT the rear plate's horizontal edges at x = 50 rather
    // than dropping or keeping them whole: there is a visible piece starting
    // exactly there.
    const std::vector<ProjectedEdge> rear1 = edgesOf(together, 1);
    CHECK(std::ranges::any_of(rear1, [](const ProjectedEdge& e) {
        return e.visibility == EdgeVisibility::Visible &&
               std::abs(std::min(e.start.x.in(units::mm), e.end.x.in(units::mm)) - 50.0) <
                   kKernelMm;
    }));

    // The front plate is in front of everything, so nothing else hides it:
    // its visible lines still span its full width.
    CHECK_THAT(leftmostVisible(edgesOf(together, 0)), WithinAbs(0.0, kKernelMm));
}

TEST_CASE("HiddenLine_ABodyEntirelyBehindAnotherDrawsNothingVisible",
          "[geometry][hiddenline][assembly][p14]") {
    // The rear box is strictly smaller in both drawn axes and strictly
    // further away, so nothing of it can be seen. A body classified on its
    // own would report every one of its edges visible.
    auto near = geometry::makeBox(Point3D{0_mm, 0_mm, 0_mm}, 100_mm, 10_mm, 60_mm);
    auto far = geometry::makeBox(Point3D{20_mm, 30_mm, 10_mm}, 40_mm, 10_mm, 30_mm);
    REQUIRE(near.has_value());
    REQUIRE(far.has_value());
    const std::vector<geometry::Body> bodies{*near, *far};

    const geometry::HiddenLineDrawing drawing = drawingOf(bodies, front());
    // (The near box has hidden edges of its own -- it hides its own back
    // face -- which is why the assertion below is about the FAR one.)
    CHECK(countOf(drawing, 0, EdgeVisibility::Visible) > 0);
    CHECK(countOf(drawing, 1, EdgeVisibility::Visible) == 0); // the whole point
    CHECK(countOf(drawing, 1, EdgeVisibility::Hidden) > 0);   // and it IS there

    // Run alone, the same box DOES show visible lines -- which is what makes
    // the assertion above a statement about occlusion rather than about the
    // box. (It has hidden lines alone too: a box hides its own back face.)
    const geometry::HiddenLineDrawing alone = drawingOf(*far, front());
    CHECK(alone.count(EdgeVisibility::Visible) > 0);
}

TEST_CASE("HiddenLine_SwappingWhichBodyIsInFrontSwapsWhatIsHidden",
          "[geometry][hiddenline][assembly][p14]") {
    // The same two shapes at swapped depths. If depth were ignored, or the
    // order of the bodies decided the answer, this would come out the same
    // way twice.
    auto small = [](Length y) {
        auto b = geometry::makeBox(Point3D{20_mm, y, 10_mm}, 40_mm, 10_mm, 30_mm);
        REQUIRE(b.has_value());
        return *b;
    };
    auto large = [](Length y) {
        auto b = geometry::makeBox(Point3D{0_mm, y, 0_mm}, 100_mm, 10_mm, 60_mm);
        REQUIRE(b.has_value());
        return *b;
    };

    // Large in front, small behind.
    const std::vector<geometry::Body> largeFirst{large(0_mm), small(30_mm)};
    const geometry::HiddenLineDrawing a = drawingOf(largeFirst, front());
    CHECK(countOf(a, 1, EdgeVisibility::Visible) == 0);

    // Small in front, large behind -- and the ORDER in the span is unchanged,
    // so only the depth differs.
    const std::vector<geometry::Body> smallInFront{large(30_mm), small(0_mm)};
    const geometry::HiddenLineDrawing b = drawingOf(smallInFront, front());
    CHECK(countOf(b, 1, EdgeVisibility::Visible) > 0);  // the small one now shows
    CHECK(countOf(b, 0, EdgeVisibility::Hidden) > 0);   // and hides part of the large one
}

TEST_CASE("HiddenLine_TwoBodiesRunTogetherAreNotTwoBodiesRunApart",
          "[geometry][hiddenline][assembly][p14]") {
    // The failure mode this whole design exists to prevent, stated as a test:
    // classifying each body on its own and concatenating gives a DIFFERENT
    // and wrong answer, and this asserts the two really do differ.
    auto near = geometry::makeBox(Point3D{0_mm, 0_mm, 0_mm}, 100_mm, 10_mm, 60_mm);
    auto far = geometry::makeBox(Point3D{20_mm, 30_mm, 10_mm}, 40_mm, 10_mm, 30_mm);
    REQUIRE(near.has_value());
    REQUIRE(far.has_value());

    const std::vector<geometry::Body> bodies{*near, *far};
    const geometry::HiddenLineDrawing together = drawingOf(bodies, front());
    const geometry::HiddenLineDrawing apartNear = drawingOf(*near, front());
    const geometry::HiddenLineDrawing apartFar = drawingOf(*far, front());

    const std::size_t visibleTogether = together.count(EdgeVisibility::Visible);
    const std::size_t visibleApart =
        apartNear.count(EdgeVisibility::Visible) + apartFar.count(EdgeVisibility::Visible);
    CHECK(visibleTogether < visibleApart);
}

TEST_CASE("HiddenLine_ASetSaysWhichBodyEachLineCameFrom",
          "[geometry][hiddenline][assembly][p14]") {
    // Provenance. Two boxes side by side, neither hiding the other, so every
    // line of each is visible -- and each line must still say which box it
    // belongs to.
    auto left = geometry::makeBox(Point3D{0_mm, 0_mm, 0_mm}, 40_mm, 10_mm, 40_mm);
    auto right = geometry::makeBox(Point3D{60_mm, 0_mm, 0_mm}, 40_mm, 10_mm, 40_mm);
    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    const std::vector<geometry::Body> bodies{*left, *right};

    const geometry::HiddenLineDrawing drawing = drawingOf(bodies, front());
    const std::vector<ProjectedEdge> zero = edgesOf(drawing, 0);
    const std::vector<ProjectedEdge> one = edgesOf(drawing, 1);
    REQUIRE_FALSE(zero.empty());
    REQUIRE_FALSE(one.empty());
    CHECK(zero.size() + one.size() == drawing.edges.size()); // nothing unattributed

    // Each box is a 40 x 40 square seen square-on, and they are 60 apart in x.
    for (const ProjectedEdge& edge : zero) {
        CHECK(edge.start.x.in(units::mm) <= 40.0 + kKernelMm);
    }
    for (const ProjectedEdge& edge : one) {
        CHECK(edge.start.x.in(units::mm) >= 60.0 - kKernelMm);
    }
}

TEST_CASE("HiddenLine_ASetWithAMissingMemberIsRefused",
          "[geometry][hiddenline][assembly][p14]") {
    auto box = geometry::makeBox(100_mm, 60_mm, 40_mm);
    REQUIRE(box.has_value());
    const std::vector<geometry::Body> withEmpty{*box, geometry::Body{}};
    const auto refused = geometry::hiddenLineDrawing(withEmpty, front());
    REQUIRE_FALSE(refused.has_value());
    CHECK(errorCode(refused) == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("draws a different thing"));

    const std::vector<geometry::Body> none{};
    const auto empty = geometry::hiddenLineDrawing(none, front());
    REQUIRE_FALSE(empty.has_value());
    CHECK_THAT(empty.error().message, ContainsSubstring("no bodies"));
}

TEST_CASE("HiddenLine_OneBodyThroughTheSetPathIsTheSingleBodyAnswer",
          "[geometry][hiddenline][assembly][p14]") {
    // There is one implementation, so a set of one must be exactly what the
    // single-body call gives -- otherwise the part path and the assembly path
    // could drift apart without anything noticing.
    auto box = geometry::makeBox(100_mm, 60_mm, 40_mm);
    REQUIRE(box.has_value());
    const std::vector<geometry::Body> one{*box};
    const geometry::HiddenLineDrawing viaSet = drawingOf(one, front());
    const geometry::HiddenLineDrawing viaBody = drawingOf(*box, front());
    CHECK(viaSet.edges == viaBody.edges);
}
