// Kernel probe (evidence, not part of the build): what OCCT 8.0.1's
// BRepOffsetAPI_ThruSections actually does for the three things
// P12-LOFT-001 needs, which P11-FEAT-009 refused.
//
// P11-FEAT-009 lofts only between sections of the *same* shape, ruled, with
// BetterCAD doing the correspondence and the kernel told not to re-match
// (CheckCompatibility(false)). This probe measures, rather than assumes,
// what is available beyond that.
//
//  A. DIFFERENT SHAPES, the kernel's own matching. Circle to square,
//     triangle to circle, square to hexagon, with CheckCompatibility(true),
//     which is what P11-FEAT-009 turned off. Does it build? Is the solid
//     valid? What volume, against the prismatoid value the same sections
//     would give if their points corresponded linearly? And -- the point --
//     is it the SAME answer when the same sections are given in a different
//     rotation of the same loop? An unstable correspondence shows up as a
//     volume that moves.
//
//  B. DIFFERENT SHAPES, BetterCAD's matching. The same pairs, but with both
//     sections first split into the same number of segments by BetterCAD
//     (here: the square's four corners against four quarter-arcs of the
//     circle), and CheckCompatibility(false). Does the kernel keep that
//     correspondence, and does the volume then match the prismatoid value?
//
//  C. SMOOTH INTERPOLATION. ThruSections(solid, ruled = false) for two
//     sections and for three. With two sections there is nothing to be
//     smooth through, so a smooth loft should be the ruled one: if it is,
//     "smooth" has an exact reference for two sections and BetterCAD can
//     define it. With three the surfaces bulge, and the prismatoid volume
//     no longer applies -- this measures by how much.
//
//  D. END CONDITIONS. Everything BRepOffsetAPI_ThruSections offers that
//     might serve as one: SetContinuity, SetParType, SetSmoothing,
//     SetMaxDegree. Each is set and the result measured, to find out which
//     (if any) changes how the surface leaves a section. A negative result
//     is a result.
//
//  E. GHOST SECTION. A section repeated a small distance along its normal
//     forces the surface to leave perpendicular to it. This measures
//     whether that is a usable way to offer "normal to profile" when the
//     API offers none, and what it costs in volume.
//
// Built against the dependency prefix; see build-and-run.cmd.
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <Approx_ParametrizationType.hxx>
#include <GeomAbs_Shape.hxx>
#include <TopExp.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstdio>
#include <string>
#include <typeinfo>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

void heading(const char* text) {
    std::printf("\n=== %s ===\n", text);
    std::fflush(stdout);
}

/// A closed polygon through @p points at height @p z.
TopoDS_Wire polygon(const std::vector<std::pair<double, double>>& points, double z) {
    BRepBuilderAPI_MakePolygon poly;
    for (const auto& [x, y] : points) {
        poly.Add(gp_Pnt(x, y, z));
    }
    poly.Close();
    return poly.Wire();
}

/// A regular @p n-gon of circumradius @p r at height @p z, its first corner
/// at angle @p start.
std::vector<std::pair<double, double>> regular(int n, double r, double start = 0.0) {
    std::vector<std::pair<double, double>> points;
    for (int i = 0; i < n; ++i) {
        const double a = start + 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
        points.emplace_back(r * std::cos(a), r * std::sin(a));
    }
    return points;
}

/// A full circle of radius @p r at height @p z, as one edge.
TopoDS_Wire circle(double r, double z) {
    const gp_Ax2 frame(gp_Pnt(0.0, 0.0, z), gp_Dir(0.0, 0.0, 1.0), gp_Dir(1.0, 0.0, 0.0));
    return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(frame, r)).Edge()).Wire();
}

/// A circle of radius @p r at height @p z split into @p n equal arcs, the
/// first starting at angle @p start: the same curve, but with @p n segments
/// so that it can correspond to an @p n-sided polygon.
TopoDS_Wire splitCircle(int n, double r, double z, double start = 0.0) {
    const gp_Ax2 frame(gp_Pnt(0.0, 0.0, z), gp_Dir(0.0, 0.0, 1.0), gp_Dir(1.0, 0.0, 0.0));
    const gp_Circ c(frame, r);
    BRepBuilderAPI_MakeWire wire;
    std::vector<TopoDS_Vertex> corners;
    for (int i = 0; i < n; ++i) {
        const double a = start + 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
        corners.push_back(BRepBuilderAPI_MakeVertex(gp_Pnt(r * std::cos(a), r * std::sin(a), z)).Vertex());
    }
    for (int i = 0; i < n; ++i) {
        wire.Add(BRepBuilderAPI_MakeEdge(c, corners[i], corners[(i + 1) % n]).Edge());
    }
    return wire.Wire();
}

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

gp_Pnt centroidOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.CentreOfMass();
}

/// Builds and reports. @p expected is the prismatoid volume the sections
/// would enclose if their points corresponded linearly; 0 to omit it.
void report(const char* label, BRepOffsetAPI_ThruSections& loft, double expected) try {
    loft.Build();
    if (!loft.IsDone()) {
        std::printf("  %-34s NOT DONE\n", label);
        std::fflush(stdout);
        return;
    }
    const TopoDS_Shape shape = loft.Shape();
    const BRepCheck_Analyzer analyzer(shape);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    const double volume = volumeOf(shape);
    const gp_Pnt centre = centroidOf(shape);
    BRepAlgoAPI_Check check(shape, /*bTestSE=*/false, /*bTestSI=*/true);
    std::printf("  %-34s valid %d  self-ok %d  faces %3d  V=%14.6f", label, analyzer.IsValid() ? 1 : 0,
                check.IsValid() ? 1 : 0, faces.Extent(), volume);
    if (expected != 0.0) {
        std::printf("  prismatoid=%14.6f  ratio %.9f", expected, volume / expected);
    }
    std::printf("  centroid z=%9.6f\n", centre.Z());
    std::fflush(stdout);
} catch (const Standard_Failure& error) {
    std::printf("  %-34s THREW %s: %s\n", label, typeid(error).name(),
                error.GetMessageString() != nullptr ? error.GetMessageString() : "");
    std::fflush(stdout);
} catch (...) {
    std::printf("  %-34s THREW (unknown)\n", label);
    std::fflush(stdout);
}

/// The area of a regular n-gon of circumradius r: n/2 r^2 sin(2 pi / n).
double polygonArea(int n, double r) {
    return 0.5 * static_cast<double>(n) * r * r * std::sin(2.0 * kPi / static_cast<double>(n));
}

} // namespace

int main() try {
    std::printf("OCCT loft probe for P12-LOFT-001 (BRepOffsetAPI_ThruSections, OCCT 8.0.1)\n");
    std::fflush(stdout);

    const double h = 30.0;

    // ---------------------------------------------------------------
    // A. Different shapes, the kernel's own correspondence.
    // ---------------------------------------------------------------
    heading("A. different shapes, CheckCompatibility(true) (the kernel matches)");
    {
        // A square of circumradius 10 to a circle of radius 5. If the points
        // corresponded linearly the halfway section would be the average of
        // the two, whose area is not a closed form; the prismatoid value
        // below uses the average of a square and a circle sampled finely,
        // and is printed only as a scale, not as a law.
        const double a0 = polygonArea(4, 10.0);
        const double a1 = kPi * 5.0 * 5.0;
        // A crude midpoint: the average of the two areas is NOT the area of
        // the averaged section, so this is a scale only.
        const double scale = h / 6.0 * (a0 + 4.0 * 0.25 * (std::sqrt(a0) + std::sqrt(a1)) *
                                                  (std::sqrt(a0) + std::sqrt(a1)) + a1);

        {
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(true);
            loft.AddWire(polygon(regular(4, 10.0), 0.0));
            loft.AddWire(circle(5.0, h));
            report("square -> circle, ruled", loft, scale);
        }
        {
            // The same square, written from another corner: the same loop,
            // a different starting vertex. A stable correspondence gives the
            // same solid.
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(true);
            loft.AddWire(polygon(regular(4, 10.0, kPi / 2.0), 0.0));
            loft.AddWire(circle(5.0, h));
            report("square from another corner", loft, scale);
        }
        {
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(true);
            loft.AddWire(polygon(regular(3, 10.0), 0.0));
            loft.AddWire(circle(5.0, h));
            report("triangle -> circle, ruled", loft, 0.0);
        }
        {
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(true);
            loft.AddWire(polygon(regular(4, 10.0), 0.0));
            loft.AddWire(polygon(regular(6, 5.0), h));
            report("square -> hexagon, ruled", loft, 0.0);
        }
        {
            // 4 corners to 4 corners is what P11-FEAT-009 already supports,
            // as a control: the prismatoid value is exact here.
            const double am = polygonArea(4, 7.5);
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(true);
            loft.AddWire(polygon(regular(4, 10.0), 0.0));
            loft.AddWire(polygon(regular(4, 5.0), h));
            report("square -> square (control)", loft,
                   h / 6.0 * (polygonArea(4, 10.0) + 4.0 * am + polygonArea(4, 5.0)));
        }
    }

    // ---------------------------------------------------------------
    // B. Different shapes, BetterCAD's correspondence: both sections
    //    split into the same number of segments first.
    // ---------------------------------------------------------------
    heading("B. different shapes, split to equal segment counts, CheckCompatibility(false)");
    {
        // A square of circumradius 10 and a circle of radius 5 split into
        // four quarter arcs starting at the same angle. Corresponding points
        // move linearly, so the cross-section at t is the average curve and
        // the prismatoid formula applies with Am the averaged section's
        // area. The averaged section of a square corner and a quarter arc
        // is a quarter of a "rounded square"; its area is computed by
        // sampling, below, and printed for scale.
        const double a0 = polygonArea(4, 10.0);
        const double a1 = kPi * 5.0 * 5.0;
        // Am by sampling the averaged curve (Green's theorem on 4096 points).
        double am = 0.0;
        {
            const int n = 4096;
            double sum = 0.0;
            const auto squareAt = [](double s) {
                // s in [0, 4): side index and position along it.
                const auto corners = regular(4, 10.0);
                const int i = static_cast<int>(s) % 4;
                const double f = s - std::floor(s);
                const auto& [x0, y0] = corners[i];
                const auto& [x1, y1] = corners[(i + 1) % 4];
                return std::pair{x0 + (x1 - x0) * f, y0 + (y1 - y0) * f};
            };
            const auto circleAt = [](double s) {
                const double a = 2.0 * kPi * s / 4.0;
                return std::pair{5.0 * std::cos(a), 5.0 * std::sin(a)};
            };
            std::vector<std::pair<double, double>> mid;
            for (int i = 0; i < n; ++i) {
                const double s = 4.0 * static_cast<double>(i) / static_cast<double>(n);
                const auto [sx, sy] = squareAt(s);
                const auto [cx, cy] = circleAt(s);
                mid.emplace_back(0.5 * (sx + cx), 0.5 * (sy + cy));
            }
            for (int i = 0; i < n; ++i) {
                const auto& [x0, y0] = mid[i];
                const auto& [x1, y1] = mid[(i + 1) % n];
                sum += x0 * y1 - x1 * y0;
            }
            am = 0.5 * std::abs(sum);
        }
        std::printf("  (the averaged section's area, sampled: %.6f mm^2)\n", am);
        std::fflush(stdout);
        const double prismatoid = h / 6.0 * (a0 + 4.0 * am + a1);

        {
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            loft.AddWire(polygon(regular(4, 10.0), 0.0));
            loft.AddWire(splitCircle(4, 5.0, h));
            report("square -> split circle, ruled", loft, prismatoid);
        }
        {
            // The circle split from another angle: a different twist, and a
            // different solid. BetterCAD must choose this deliberately.
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            loft.AddWire(polygon(regular(4, 10.0), 0.0));
            loft.AddWire(splitCircle(4, 5.0, h, kPi / 4.0));
            report("square -> circle split at 45 deg", loft, prismatoid);
        }
        {
            // A triangle to a circle split into three arcs.
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            loft.AddWire(polygon(regular(3, 10.0), 0.0));
            loft.AddWire(splitCircle(3, 5.0, h));
            report("triangle -> split circle, ruled", loft, 0.0);
        }
        {
            // A square to a hexagon needs a common count: split both to 12.
            // Here the square's sides are split into 3 and the hexagon's
            // into 2, which BetterCAD would have to do itself.
            std::vector<std::pair<double, double>> square12;
            const auto corners4 = regular(4, 10.0);
            for (int i = 0; i < 4; ++i) {
                const auto& [x0, y0] = corners4[i];
                const auto& [x1, y1] = corners4[(i + 1) % 4];
                for (int k = 0; k < 3; ++k) {
                    const double f = static_cast<double>(k) / 3.0;
                    square12.emplace_back(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f);
                }
            }
            std::vector<std::pair<double, double>> hex12;
            const auto corners6 = regular(6, 5.0);
            for (int i = 0; i < 6; ++i) {
                const auto& [x0, y0] = corners6[i];
                const auto& [x1, y1] = corners6[(i + 1) % 6];
                for (int k = 0; k < 2; ++k) {
                    const double f = static_cast<double>(k) / 2.0;
                    hex12.emplace_back(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f);
                }
            }
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            loft.AddWire(polygon(square12, 0.0));
            loft.AddWire(polygon(hex12, h));
            report("square(12) -> hexagon(12), ruled", loft, 0.0);
        }
    }

    // ---------------------------------------------------------------
    // C. Smooth interpolation.
    // ---------------------------------------------------------------
    heading("C. smooth interpolation (ruled = false)");
    {
        const double a0 = kPi * 100.0;
        const double a1 = kPi * 25.0;
        const double am = kPi * 7.5 * 7.5;
        const double twoSections = h / 6.0 * (a0 + 4.0 * am + a1);
        std::printf("  two coaxial circles r 10 -> r 5 over %.0f mm: frustum %.6f mm^3\n", h,
                    kPi * h / 3.0 * (100.0 + 50.0 + 25.0));
        std::fflush(stdout);
        {
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(5.0, h));
            report("two circles, ruled", loft, twoSections);
        }
        {
            BRepOffsetAPI_ThruSections loft(true, false);
            loft.CheckCompatibility(false);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(5.0, h));
            report("two circles, smooth", loft, twoSections);
        }
        {
            // Three sections: ruled gives two frustums; smooth bulges.
            const double ruled = kPi * 25.0 / 3.0 * (100.0 + 50.0 + 25.0) +
                                 kPi * 25.0 / 3.0 * (25.0 + 50.0 + 100.0);
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(5.0, 25.0));
            loft.AddWire(circle(10.0, 50.0));
            report("three circles, ruled", loft, ruled);
        }
        {
            const double ruled = kPi * 25.0 / 3.0 * (100.0 + 50.0 + 25.0) +
                                 kPi * 25.0 / 3.0 * (25.0 + 50.0 + 100.0);
            BRepOffsetAPI_ThruSections loft(true, false);
            loft.CheckCompatibility(false);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(5.0, 25.0));
            loft.AddWire(circle(10.0, 50.0));
            report("three circles, smooth", loft, ruled);
        }
        {
            // Squares, three sections, smooth: how far does a flat-sided
            // loft bulge?
            const double ruled = 25.0 / 6.0 * (polygonArea(4, 10.0) + 4.0 * polygonArea(4, 7.5) +
                                               polygonArea(4, 5.0)) +
                                 25.0 / 6.0 * (polygonArea(4, 5.0) + 4.0 * polygonArea(4, 7.5) +
                                               polygonArea(4, 10.0));
            BRepOffsetAPI_ThruSections loft(true, false);
            loft.CheckCompatibility(false);
            loft.AddWire(polygon(regular(4, 10.0), 0.0));
            loft.AddWire(polygon(regular(4, 5.0), 25.0));
            loft.AddWire(polygon(regular(4, 10.0), 50.0));
            report("three squares, smooth", loft, ruled);
        }
    }

    // ---------------------------------------------------------------
    // D. What the API offers that might be an end condition.
    // ---------------------------------------------------------------
    heading("D. ThruSections settings (looking for an end condition)");
    {
        const double frustum = kPi * h / 3.0 * (100.0 + 50.0 + 25.0);
        const auto build = [&](const char* label, auto&& configure) {
            BRepOffsetAPI_ThruSections loft(true, false);
            loft.CheckCompatibility(false);
            configure(loft);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(5.0, h));
            report(label, loft, frustum);
        };
        build("smooth, default", [](BRepOffsetAPI_ThruSections&) {});
        build("smooth, Continuity C0", [](BRepOffsetAPI_ThruSections& l) { l.SetContinuity(GeomAbs_C0); });
        build("smooth, Continuity C1", [](BRepOffsetAPI_ThruSections& l) { l.SetContinuity(GeomAbs_C1); });
        build("smooth, Continuity C2", [](BRepOffsetAPI_ThruSections& l) { l.SetContinuity(GeomAbs_C2); });
        build("smooth, ParType Centripetal",
              [](BRepOffsetAPI_ThruSections& l) { l.SetParType(Approx_Centripetal); });
        build("smooth, ParType ChordLength",
              [](BRepOffsetAPI_ThruSections& l) { l.SetParType(Approx_ChordLength); });
        build("smooth, ParType IsoParametric",
              [](BRepOffsetAPI_ThruSections& l) { l.SetParType(Approx_IsoParametric); });
        build("smooth, MaxDegree 3", [](BRepOffsetAPI_ThruSections& l) { l.SetMaxDegree(3); });
        build("smooth, MaxDegree 8", [](BRepOffsetAPI_ThruSections& l) { l.SetMaxDegree(8); });
        build("smooth, Smoothing on", [](BRepOffsetAPI_ThruSections& l) { l.SetSmoothing(true); });
        std::printf("  The class offers no tangent, normal or direction end condition: its settings choose how the\n"
                    "  surface is approximated, not how it leaves a section.\n");
        std::fflush(stdout);
    }

    // ---------------------------------------------------------------
    // E. A ghost section: the end section repeated a little way along
    //    its normal, to force the surface to leave perpendicular.
    // ---------------------------------------------------------------
    heading("E. ghost section (a repeated section a little way along the normal)");
    {
        const double frustum = kPi * h / 3.0 * (100.0 + 50.0 + 25.0);
        for (const double gap : {0.01, 0.1, 1.0}) {
            BRepOffsetAPI_ThruSections loft(true, false);
            loft.CheckCompatibility(false);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(10.0, gap));
            loft.AddWire(circle(5.0, h));
            const std::string label = std::string("smooth, ghost ") + std::to_string(gap) + " mm";
            report(label.c_str(), loft, frustum);
        }
        std::printf("  A ghost section changes the solid: it adds a straight collar of its own length and moves\n"
                    "  the volume away from the frustum. Recorded as measured, not recommended.\n");
        std::fflush(stdout);
    }

    // ---------------------------------------------------------------
    // F. Which parameter does a smooth loft interpolate in? Three
    //    circles at UNEQUAL heights. If the kernel interpolates in the
    //    section index (uniform), the radius at height z is the
    //    quadratic through (0, 10), (1, 5), (2, 10) evaluated at the
    //    index, and the volume follows one law; if it interpolates in
    //    z, another. Both are computed here and the measurement decides.
    // ---------------------------------------------------------------
    heading("F. smooth, three circles at unequal heights (which parameter?)");
    {
        const double z1 = 10.0;
        const double z2 = 50.0;
        // Uniform in the section index: r(u) = 10 - 20 u (1 - u) with u the
        // index/2, and z(u) piecewise linear through the given heights. The
        // volume is the integral of pi r^2 dz along that.
        const int n = 200000;
        double uniform = 0.0;
        for (int i = 0; i < n; ++i) {
            const double u0 = static_cast<double>(i) / n;
            const double u1 = static_cast<double>(i + 1) / n;
            const auto radius = [](double u) { return 10.0 - 20.0 * u * (1.0 - u); };
            const auto height = [&](double u) {
                return u <= 0.5 ? z1 * (u / 0.5) : z1 + (z2 - z1) * ((u - 0.5) / 0.5);
            };
            const double rm = radius(0.5 * (u0 + u1));
            uniform += kPi * rm * rm * (height(u1) - height(u0));
        }
        // Interpolating in z: the quadratic through (0, 10), (z1, 5), (z2, 10).
        double inZ = 0.0;
        for (int i = 0; i < n; ++i) {
            const double a0 = z2 * (0.0 - z1) * (0.0 - z2);
            (void)a0;
            const auto radius = [&](double z) {
                // Lagrange through (0,10), (z1,5), (z2,10).
                const double l0 = (z - z1) * (z - z2) / ((0.0 - z1) * (0.0 - z2));
                const double l1 = (z - 0.0) * (z - z2) / ((z1 - 0.0) * (z1 - z2));
                const double l2 = (z - 0.0) * (z - z1) / ((z2 - 0.0) * (z2 - z1));
                return 10.0 * l0 + 5.0 * l1 + 10.0 * l2;
            };
            const double zz = z2 * (static_cast<double>(i) + 0.5) / n;
            const double r = radius(zz);
            inZ += kPi * r * r * (z2 / n);
        }
        const double ruled = kPi * z1 / 3.0 * (100.0 + 50.0 + 25.0) +
                             kPi * (z2 - z1) / 3.0 * (25.0 + 50.0 + 100.0);
        std::printf("  laws: uniform-in-index %.6f   in-z %.6f   ruled %.6f\n", uniform, inZ, ruled);
        std::fflush(stdout);
        {
            BRepOffsetAPI_ThruSections loft(true, false);
            loft.CheckCompatibility(false);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(5.0, z1));
            loft.AddWire(circle(10.0, z2));
            report("three circles unequal, smooth", loft, uniform);
        }
        {
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            loft.AddWire(circle(10.0, 0.0));
            loft.AddWire(circle(5.0, z1));
            loft.AddWire(circle(10.0, z2));
            report("three circles unequal, ruled", loft, ruled);
        }
    }

    // ---------------------------------------------------------------
    // G. Four sections, equally spaced, smooth: the cubic through the
    //    four radii, if the law holds.
    // ---------------------------------------------------------------
    heading("G. smooth, four equally spaced circles (a cubic, if the law holds)");
    {
        const double top = 60.0;
        const double radii[4] = {10.0, 5.0, 8.0, 4.0};
        const int n = 200000;
        double law = 0.0;
        for (int i = 0; i < n; ++i) {
            const double u = (static_cast<double>(i) + 0.5) / n;
            // Lagrange through (0, r0), (1/3, r1), (2/3, r2), (1, r3).
            const double nodes[4] = {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0};
            double r = 0.0;
            for (int k = 0; k < 4; ++k) {
                double term = radii[k];
                for (int m = 0; m < 4; ++m) {
                    if (m != k) {
                        term *= (u - nodes[m]) / (nodes[k] - nodes[m]);
                    }
                }
                r += term;
            }
            law += kPi * r * r * (top / n);
        }
        double ruled = 0.0;
        for (int k = 0; k < 3; ++k) {
            ruled += kPi * (top / 3.0) / 3.0 *
                     (radii[k] * radii[k] + radii[k] * radii[k + 1] + radii[k + 1] * radii[k + 1]);
        }
        std::printf("  laws: cubic-in-index %.6f   ruled %.6f\n", law, ruled);
        std::fflush(stdout);
        {
            BRepOffsetAPI_ThruSections loft(true, false);
            loft.CheckCompatibility(false);
            for (int k = 0; k < 4; ++k) {
                loft.AddWire(circle(radii[k], top * static_cast<double>(k) / 3.0));
            }
            report("four circles, smooth", loft, law);
        }
        {
            BRepOffsetAPI_ThruSections loft(true, true);
            loft.CheckCompatibility(false);
            for (int k = 0; k < 4; ++k) {
                loft.AddWire(circle(radii[k], top * static_cast<double>(k) / 3.0));
            }
            report("four circles, ruled", loft, ruled);
        }
    }

    return 0;
} catch (const Standard_Failure& error) {
    std::printf("\nABORTED: %s: %s\n", typeid(error).name(),
                error.GetMessageString() != nullptr ? error.GetMessageString() : "");
    std::fflush(stdout);
    return 1;
}
