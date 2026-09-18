// Kernel probe (evidence, not part of the build): what OCCT 8.0.1's
// BRepOffsetAPI_MakePipeShell actually does for the three things
// P12-SWEEP-001 needs, which P11's planar binormal sweep did not.
//
// BetterCAD's existing sweep uses SetMode(gp_Dir binormal) with the path
// plane's normal, which only means something while the path stays in that
// plane. This probe measures, rather than assumes, what the alternatives do.
//
//  A. SPATIAL SPINE. A path of two runs meeting tangentially but lying in
//     different planes (a quarter arc in Z=0 followed by a quarter arc that
//     leaves it), swept with each frame mode:
//       - SetMode(gp_Dir)            fixed binormal
//       - SetMode(true)     Frenet
//       - SetMode(false)    corrected Frenet
//       - SetMode(gp_Ax2)            fixed trihedron
//     For each: does it build, is the solid valid, what volume does it
//     enclose, and -- the point of the probe -- where does a marked corner
//     of the profile end up? A rectangular profile makes the orientation
//     observable: its long axis is reported at the start and at the end.
//
//  B. INFLECTION. An S-shaped spatial path (two arcs turning opposite ways)
//     with each mode, to see whether the section flips where the curvature
//     reverses. Frenet's binormal reverses at an inflection; corrected
//     Frenet is supposed not to.
//
//  C. LOW CURVATURE. A path of two straight segments meeting tangentially
//     (zero curvature throughout), where the Frenet frame is undefined.
//
//  D. GUIDE CURVE (auxiliary spine). SetMode(auxiliary wire, ...) with each
//     combination of CurvilinearEquivalence and BRepFill_TypeOfContact, on
//     a straight spine with a straight guide and with a helical guide.
//     Reports whether it builds and what the section does.
//
//  E. TWIST BY HELICAL GUIDE. A straight spine with a helical auxiliary
//     spine of one known total turn, to see whether the section rotation
//     tracks the guide's angle, and whether the rotation is uniform in the
//     path parameter. Reports the measured angle at the end.
//
//  F. SCALING LAW. Add(profile, law, ...) exists for scaling; this checks
//     whether any rotation law is available at all (it is not, so this is
//     recorded as a negative result).
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       sweep_frame_probe.cpp -L<deps>/lib -lTKOffset -lTKBO -lTKShHealing
//       -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d
//       -lTKG3d -lTKMath -lTKernel
// See build-and-run.cmd.
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GProp_GProps.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <Standard_Failure.hxx>

#include <typeinfo>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

void heading(const char* text) {
    std::printf("\n=== %s ===\n", text);
}

/// A rectangular profile 8 x 2 mm centred on @p centre, lying in the plane
/// with normal @p normal and long axis @p longAxis. Its long axis makes the
/// section's rotation about the path observable.
TopoDS_Wire rectangle(const gp_Pnt& centre, const gp_Dir& normal, const gp_Dir& longAxis, double halfLong,
                      double halfShort) {
    const gp_Dir shortAxis(gp_Vec(normal).Crossed(gp_Vec(longAxis)));
    const gp_Vec along = gp_Vec(longAxis) * halfLong;
    const gp_Vec across = gp_Vec(shortAxis) * halfShort;
    BRepBuilderAPI_MakePolygon polygon;
    polygon.Add(centre.Translated(along + across));
    polygon.Add(centre.Translated(-along + across));
    polygon.Add(centre.Translated(-along - across));
    polygon.Add(centre.Translated(along - across));
    polygon.Close();
    return polygon.Wire();
}

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

/// The longest edge of @p face's wire, as a direction: for a rectangular
/// section this is its long axis, so the section's rotation can be read off.
bool longestEdgeDirection(const TopoDS_Shape& face, gp_Dir& out, gp_Pnt& midpoint) {
    double best = 0.0;
    bool found = false;
    for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        TopoDS_Vertex a;
        TopoDS_Vertex b;
        TopExp::Vertices(edge, a, b);
        if (a.IsNull() || b.IsNull()) {
            continue;
        }
        const gp_Pnt pa = BRep_Tool::Pnt(a);
        const gp_Pnt pb = BRep_Tool::Pnt(b);
        const double length = pa.Distance(pb);
        if (length > best + 1e-9) {
            best = length;
            out = gp_Dir(gp_Vec(pa, pb));
            midpoint = gp_Pnt((pa.X() + pb.X()) / 2.0, (pa.Y() + pb.Y()) / 2.0, (pa.Z() + pb.Z()) / 2.0);
            found = true;
        }
    }
    return found;
}

void report(const char* label, BRepOffsetAPI_MakePipeShell& pipe, double pappus) try {
    pipe.Build();
    if (!pipe.IsDone()) {
        std::printf("  %-22s NOT DONE\n", label);
        std::fflush(stdout);
        return;
    }
    if (!pipe.MakeSolid()) {
        std::printf("  %-22s no solid\n", label);
        std::fflush(stdout);
        return;
    }
    const TopoDS_Shape shape = pipe.Shape();
    const BRepCheck_Analyzer analyzer(shape);
    const double volume = volumeOf(shape);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    std::printf("  %-22s valid %d  faces %3d  V=%12.6f  Pappus=%12.6f  ratio %.9f\n", label,
                analyzer.IsValid() ? 1 : 0, faces.Extent(), volume, pappus,
                pappus != 0.0 ? volume / pappus : 0.0);

    gp_Dir firstAxis;
    gp_Pnt firstMid;
    gp_Dir lastAxis;
    gp_Pnt lastMid;
    const bool haveFirst = longestEdgeDirection(pipe.FirstShape(), firstAxis, firstMid);
    const bool haveLast = longestEdgeDirection(pipe.LastShape(), lastAxis, lastMid);
    if (haveFirst && haveLast) {
        const double angle = firstAxis.Angle(lastAxis) * 180.0 / kPi;
        std::printf("  %-22s start long axis (%7.4f,%7.4f,%7.4f)  end (%7.4f,%7.4f,%7.4f)  between %8.4f deg\n", "",
                    firstAxis.X(), firstAxis.Y(), firstAxis.Z(), lastAxis.X(), lastAxis.Y(), lastAxis.Z(), angle);
    }
    std::fflush(stdout);
} catch (const Standard_Failure& error) {
    // A kernel throw is a result: it says this mode cannot build this case.
    std::printf("  %-22s THREW %s: %s\n", label, typeid(error).name(),
                error.GetMessageString() != nullptr ? error.GetMessageString() : "");
    std::fflush(stdout);
} catch (...) {
    std::printf("  %-22s THREW (unknown)\n", label);
    std::fflush(stdout);
}

/// An arc from @p from to @p to about @p centre (kept for future cases).
TopoDS_Edge arc(const gp_Pnt& centre, const gp_Dir& normal, const gp_Pnt& from, const gp_Pnt& to) {
    const gp_Ax2 frame(centre, normal, gp_Dir(gp_Vec(centre, from)));
    const gp_Circ circle(frame, centre.Distance(from));
    return BRepBuilderAPI_MakeEdge(circle, BRepBuilderAPI_MakeVertex(from).Vertex(),
                                   BRepBuilderAPI_MakeVertex(to).Vertex())
        .Edge();
}

TopoDS_Edge line(const gp_Pnt& from, const gp_Pnt& to) {
    return BRepBuilderAPI_MakeEdge(from, to).Edge();
}

/// A helix of @p turns total turn about the Z axis, radius @p radius, from
/// z = 0 to z = @p height, as a B-spline through @p samples points.
TopoDS_Wire helix(double radius, double height, double turns, int samples) {
    TColgp_Array1OfPnt points(1, samples);
    for (int i = 1; i <= samples; ++i) {
        const double u = static_cast<double>(i - 1) / static_cast<double>(samples - 1);
        const double angle = 2.0 * kPi * turns * u;
        points.SetValue(i, gp_Pnt(radius * std::cos(angle), radius * std::sin(angle), height * u));
    }
    GeomAPI_PointsToBSpline fit(points);
    return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(fit.Curve()).Edge()).Wire();
}

} // namespace

int main() try {
    std::printf("OCCT sweep frame probe for P12-SWEEP-001\n");
    std::fflush(stdout);
    std::printf("Profile: an 8 x 2 mm rectangle, so its long axis shows the section's rotation.\n");

    const double halfLong = 4.0;
    const double halfShort = 1.0;
    const double area = 4.0 * halfLong * halfShort;

    // ---------------------------------------------------------------
    // A. Spatial spine: a helix, the canonical non-planar path. Radius
    //    20 mm, 40 mm tall, half a turn, so the tangent leaves every
    //    plane. Pappus: the section is centred on the spine, so
    //    V = area x spine length.
    // ---------------------------------------------------------------
    heading("A. spatial spine (a helix: radius 20, height 40, half a turn)");
    {
        const double radius = 20.0;
        const double height = 40.0;
        const double turns = 0.5;
        const TopoDS_Wire spine = helix(radius, height, turns, 64);
        // A helix of pitch angle a has length sqrt((2 pi R n)^2 + h^2).
        const double sweptAngle = 2.0 * kPi * turns;
        const double spineLength = std::sqrt(sweptAngle * radius * sweptAngle * radius + height * height);
        const double pappus = area * spineLength;
        // The helix starts at (R, 0, 0); its tangent there is
        // (0, 2 pi R n, h) normalized. The section must be across it.
        const gp_Dir startTangent(0.0, sweptAngle * radius, height);
        const TopoDS_Wire profile =
            rectangle(gp_Pnt(radius, 0.0, 0.0), startTangent, gp_Dir(1.0, 0.0, 0.0), halfLong, halfShort);
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(gp_Dir(0.0, 0.0, 1.0));
            pipe.Add(profile, false, false);
            report("binormal +Z", pipe, pappus);
        }
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(true); // Frenet
            pipe.Add(profile, false, false);
            report("Frenet", pipe, pappus);
        }
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(false); // corrected Frenet
            pipe.Add(profile, false, false);
            report("corrected Frenet", pipe, pappus);
        }
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0), gp_Dir(1.0, 0.0, 0.0)));
            pipe.Add(profile, false, false);
            report("fixed trihedron", pipe, pappus);
        }
    }

    // ---------------------------------------------------------------
    // B. Spatial polyline: three straight runs, each in a different
    //    plane, meeting at two right-angled corners. This is the shape a
    //    path assembled from several sketches takes, and the one
    //    BetterCAD's mitred corners must handle.
    // ---------------------------------------------------------------
    heading("B. spatial polyline (three runs, two right-angled corners)");
    {
        const gp_Pnt a(0.0, 0.0, 0.0);
        const gp_Pnt b(40.0, 0.0, 0.0);
        const gp_Pnt c(40.0, 40.0, 0.0);
        const gp_Pnt d(40.0, 40.0, 40.0);
        BRepBuilderAPI_MakeWire wire(line(a, b));
        wire.Add(line(b, c));
        wire.Add(line(c, d));
        const TopoDS_Wire spine = wire.Wire();
        const double pappus = area * 120.0;
        const TopoDS_Wire profile = rectangle(a, gp_Dir(1.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0), halfLong, halfShort);
        const auto build = [&](const char* label, auto&& setMode) {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            setMode(pipe);
            pipe.SetTransitionMode(BRepBuilderAPI_RightCorner); // as BetterCAD does
            pipe.Add(profile, false, false);
            report(label, pipe, pappus);
        };
        build("binormal +Z", [](BRepOffsetAPI_MakePipeShell& pipe) { pipe.SetMode(gp_Dir(0.0, 0.0, 1.0)); });
        build("binormal +Y", [](BRepOffsetAPI_MakePipeShell& pipe) { pipe.SetMode(gp_Dir(0.0, 1.0, 0.0)); });
        build("Frenet", [](BRepOffsetAPI_MakePipeShell& pipe) { pipe.SetMode(true); });
        build("corrected Frenet", [](BRepOffsetAPI_MakePipeShell& pipe) { pipe.SetMode(false); });
    }

    // ---------------------------------------------------------------
    // C. Zero curvature: two straight segments in line.
    // ---------------------------------------------------------------
    heading("C. zero curvature (two collinear straight segments)");
    {
        const gp_Pnt start(0.0, 0.0, 0.0);
        BRepBuilderAPI_MakeWire wire(line(start, gp_Pnt(0.0, 20.0, 0.0)));
        wire.Add(line(gp_Pnt(0.0, 20.0, 0.0), gp_Pnt(0.0, 40.0, 0.0)));
        const TopoDS_Wire spine = wire.Wire();
        const double pappus = area * 40.0;
        const TopoDS_Wire profile =
            rectangle(start, gp_Dir(0.0, 1.0, 0.0), gp_Dir(1.0, 0.0, 0.0), halfLong, halfShort);
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(gp_Dir(0.0, 0.0, 1.0));
            pipe.Add(profile, false, false);
            report("binormal +Z", pipe, pappus);
        }
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(true);
            pipe.Add(profile, false, false);
            report("Frenet", pipe, pappus);
        }
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(false);
            pipe.Add(profile, false, false);
            report("corrected Frenet", pipe, pappus);
        }
    }

    // ---------------------------------------------------------------
    // D. Guide curve: a straight spine with a straight parallel guide.
    // ---------------------------------------------------------------
    heading("D. guide curve, straight spine and straight guide 10 mm away");
    {
        const gp_Pnt start(0.0, 0.0, 0.0);
        const TopoDS_Wire spine = BRepBuilderAPI_MakeWire(line(start, gp_Pnt(0.0, 40.0, 0.0))).Wire();
        const TopoDS_Wire guide =
            BRepBuilderAPI_MakeWire(line(gp_Pnt(10.0, 0.0, 0.0), gp_Pnt(10.0, 40.0, 10.0))).Wire();
        const double pappus = area * 40.0;
        const TopoDS_Wire profile =
            rectangle(start, gp_Dir(0.0, 1.0, 0.0), gp_Dir(1.0, 0.0, 0.0), halfLong, halfShort);
        const char* contactNames[] = {"NoContact", "Contact", "ContactOnBorder"};
        const BRepFill_TypeOfContact contacts[] = {BRepFill_NoContact, BRepFill_Contact,
                                                   BRepFill_ContactOnBorder};
        for (int equivalence = 0; equivalence < 2; ++equivalence) {
            for (int c = 0; c < 3; ++c) {
                BRepOffsetAPI_MakePipeShell pipe(spine);
                pipe.SetMode(guide, equivalence == 1 ? true : false, contacts[c]);
                pipe.Add(profile, false, false);
                const std::string label =
                    std::string(equivalence == 1 ? "equiv " : "noequiv ") + contactNames[c];
                report(label.c_str(), pipe, pappus);
            }
        }
    }

    // ---------------------------------------------------------------
    // E. Twist by a helical guide: one quarter turn over 40 mm.
    // ---------------------------------------------------------------
    heading("E. helical guide (0.25 turn over 40 mm, radius 10)");
    {
        const gp_Pnt start(0.0, 0.0, 0.0);
        // Spine along +Z so the helix can wrap it.
        const TopoDS_Wire spine = BRepBuilderAPI_MakeWire(line(start, gp_Pnt(0.0, 0.0, 40.0))).Wire();
        const double pappus = area * 40.0;
        const TopoDS_Wire profile =
            rectangle(start, gp_Dir(0.0, 0.0, 1.0), gp_Dir(1.0, 0.0, 0.0), halfLong, halfShort);
        for (const double turns : {0.25, 0.5, 1.0}) {
            const TopoDS_Wire guide = helix(10.0, 40.0, turns, 64);
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(guide, false, BRepFill_NoContact);
            pipe.Add(profile, false, false);
            const std::string label = std::string("helix ") + std::to_string(turns) + " turn";
            report(label.c_str(), pipe, pappus);
        }
    }

    // ---------------------------------------------------------------
    // G. Planar path, binormal against corrected Frenet. A planar path's
    //    plane normal never rotates about the tangent, so the two frames
    //    should agree exactly. If they do, BetterCAD has one convention.
    // ---------------------------------------------------------------
    heading("G. planar path (line, tangent quarter arc, line) in the XY plane");
    {
        const gp_Pnt a(0.0, 0.0, 0.0);
        const gp_Pnt b(40.0, 0.0, 0.0);
        const gp_Pnt c(60.0, 20.0, 0.0);
        // Arc centre (40, 20, 0), from b to c, turning left (about +Z).
        BRepBuilderAPI_MakeWire wire(line(a, b));
        wire.Add(arc(gp_Pnt(40.0, 20.0, 0.0), gp_Dir(0.0, 0.0, 1.0), b, c));
        wire.Add(line(c, gp_Pnt(60.0, 60.0, 0.0)));
        const TopoDS_Wire spine = wire.Wire();
        const double pappus = area * (40.0 + 20.0 * kPi / 2.0 + 40.0);
        const TopoDS_Wire profile = rectangle(a, gp_Dir(1.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0), halfLong, halfShort);
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(gp_Dir(0.0, 0.0, 1.0));
            pipe.Add(profile, false, false);
            report("binormal +Z", pipe, pappus);
        }
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(false);
            pipe.Add(profile, false, false);
            report("corrected Frenet", pipe, pappus);
        }
        {
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(true);
            pipe.Add(profile, false, false);
            report("Frenet", pipe, pappus);
        }
    }

    // ---------------------------------------------------------------
    // H. A generated auxiliary spine on a CURVED planar path: the quarter
    //    arc of G, with a companion curve 10 mm away that turns by theta
    //    about the path's own frame (T, N = B x T, B = +Z). If the
    //    section follows it, BetterCAD can define its own twist.
    // ---------------------------------------------------------------
    heading("H. generated twist guide on a quarter arc (radius 40, in XY)");
    {
        const double bend = 40.0;
        const gp_Pnt start(bend, 0.0, 0.0);
        // A quarter arc about the origin, from (40, 0, 0) to (0, 40, 0).
        const TopoDS_Wire spine = BRepBuilderAPI_MakeWire(
                                      arc(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0), start,
                                          gp_Pnt(0.0, bend, 0.0)))
                                      .Wire();
        const double pappus = area * bend * kPi / 2.0;
        // The section starts across the tangent (0, 1, 0) at (40, 0, 0).
        const TopoDS_Wire profile =
            rectangle(start, gp_Dir(0.0, 1.0, 0.0), gp_Dir(1.0, 0.0, 0.0), halfLong, halfShort);
        for (const double degrees : {90.0, 180.0, 360.0}) {
            const double total = degrees * kPi / 180.0;
            // The companion: at u, the path point plus 10 mm along
            // cos(theta) N + sin(theta) B, with B = +Z and N = B x T
            // (outward here). theta(u) = u theta_total.
            const int samples = 96;
            TColgp_Array1OfPnt points(1, samples);
            for (int i = 1; i <= samples; ++i) {
                const double u = static_cast<double>(i - 1) / static_cast<double>(samples - 1);
                const double along = u * kPi / 2.0; // the arc's own angle
                const gp_Pnt on(bend * std::cos(along), bend * std::sin(along), 0.0);
                // T = (-sin, cos, 0); B = (0, 0, 1); N = B x T = (-cos, -sin, 0).
                const gp_Vec n(-std::cos(along), -std::sin(along), 0.0);
                const gp_Vec b(0.0, 0.0, 1.0);
                const double theta = u * total;
                const gp_Vec offset = (n * std::cos(theta) + b * std::sin(theta)) * 10.0;
                points.SetValue(i, on.Translated(offset));
            }
            GeomAPI_PointsToBSpline fit(points);
            const TopoDS_Wire guide = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(fit.Curve()).Edge()).Wire();
            BRepOffsetAPI_MakePipeShell pipe(spine);
            pipe.SetMode(guide, false, BRepFill_NoContact);
            pipe.Add(profile, false, false);
            const std::string label = std::string("twist ") + std::to_string(static_cast<int>(degrees)) + " deg";
            report(label.c_str(), pipe, pappus);
        }
    }

    // ---------------------------------------------------------------
    // F. Rotation law: MakePipeShell offers a scaling law only.
    // ---------------------------------------------------------------
    heading("F. rotation law");
    std::fflush(stdout);
    std::printf("  BRepOffsetAPI_MakePipeShell::Add(profile, Law_Function, ...) scales the section; the API\n"
                "  offers no rotation law. Recorded as a negative result: a twist must come from the frame.\n");
    std::fflush(stdout);

    return 0;
} catch (const Standard_Failure& error) {
    std::printf("\nABORTED building a probe case: %s: %s\n", typeid(error).name(),
                error.GetMessageString() != nullptr ? error.GetMessageString() : "");
    std::fflush(stdout);
    return 1;
}
