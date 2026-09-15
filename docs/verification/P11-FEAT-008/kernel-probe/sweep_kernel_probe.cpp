// Kernel probe for P11-FEAT-008 Sweep (evidence, not part of the build).
// Raw OCCT 8.0.1: how BRepOffsetAPI_MakePipe and BRepOffsetAPI_MakePipeShell
// behave on analytic sweep cases, which chose the builder behind makeSweep().
// One case and builder per process, so a crash shows as an exit code:
//   sweep_kernel_probe <case> <builder>
// Builders:
//   pipe      MakePipe(spine, face), corrected Frenet trihedron (OCCT default)
//   pipe-cn   MakePipe(spine, face), constant normal
//   shell-t   MakePipeShell(spine), SetMode(binormal), transformed corners (default)
//   shell-r   the same with right (mitred) corners
//   shell-o   the same with round corners
//   shell-rh  shell-r, each loop swept on its own and the holes cut: makeSweep's choice
// Each line: solids, BRepCheck validity, volume (and the analytic volume with
// its relative error when there is one), bounds, centre of mass, time, the
// face types, and BRepAlgoAPI_Check's self-interference result (selfcheck).
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       sweep_kernel_probe.cpp -L<deps>/lib -lTKOffset -lTKFillet -lTKBool
//       -lTKBO -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d
//       -lTKG3d -lTKShHealing -lTKMath -lTKernel
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;

TopoDS_Edge line(gp_Pnt a, gp_Pnt b) { return BRepBuilderAPI_MakeEdge(a, b); }

// Arc about `center` with axis `normal` from a to b (counter-clockwise about normal).
TopoDS_Edge arc(gp_Pnt center, gp_Dir normal, gp_Pnt a, gp_Pnt b) {
    gp_Circ circ(gp_Ax2(center, normal, gp_Vec(center, a)), center.Distance(a));
    return BRepBuilderAPI_MakeEdge(circ, a, b);
}

TopoDS_Wire wire(const std::vector<TopoDS_Edge>& edges) {
    BRepBuilderAPI_MakeWire w;
    for (const auto& e : edges) {
        w.Add(e);
    }
    return w.Wire();
}

// A circle of radius r centred at c in the plane with normal n.
TopoDS_Wire circleWire(gp_Pnt c, gp_Dir n, double r) {
    return wire({BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(c, n), r))});
}

// A w x h rectangle centred at c in the plane with normal n, width along u.
TopoDS_Wire rectWire(gp_Pnt c, gp_Dir n, gp_Dir u, double w, double h) {
    const gp_Vec U(u);
    const gp_Vec V = gp_Vec(n).Crossed(U);
    const gp_Pnt p0 = c.Translated(-U * (w / 2) - V * (h / 2));
    const gp_Pnt p1 = c.Translated(U * (w / 2) - V * (h / 2));
    const gp_Pnt p2 = c.Translated(U * (w / 2) + V * (h / 2));
    const gp_Pnt p3 = c.Translated(-U * (w / 2) + V * (h / 2));
    return wire({line(p0, p1), line(p1, p2), line(p2, p3), line(p3, p0)});
}

struct Case {
    TopoDS_Wire spine;
    std::vector<TopoDS_Wire> loops; // outer first, then holes
    gp_Dir binormal{0, 0, 1};       // the path plane's normal
    double expected = 0.0;          // analytic volume, 0 if none
    const char* formula = "";
};

bool makeCase(const std::string& name, Case& c) {
    const gp_Dir X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    if (name == "straight-rect") {        // 10 x 20 along 100 mm
        c.spine = wire({line({0, 0, 0}, {0, 0, 100})});
        c.loops = {rectWire({0, 0, 0}, Z, X, 10, 20)};
        c.binormal = X;
        c.expected = 20000;
        c.formula = "A L = 200 x 100";
    } else if (name == "straight-circle") { // r 5 along 100 mm
        c.spine = wire({line({0, 0, 0}, {0, 0, 100})});
        c.loops = {circleWire({0, 0, 0}, Z, 5)};
        c.binormal = X;
        c.expected = 2500 * pi;
        c.formula = "pi r^2 L";
    } else if (name == "quarter") {        // r 2 along a 90 deg arc, R 20 (path in XY, centre origin)
        c.spine = wire({arc({0, 0, 0}, Z, {20, 0, 0}, {0, 20, 0})});
        c.loops = {circleWire({20, 0, 0}, Y, 2)};
        c.expected = 40 * pi * pi;
        c.formula = "pi r^2 R theta = 40 pi^2";
    } else if (name == "torus") {          // r 2 around a full circle, R 20
        c.spine = wire({BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2({0, 0, 0}, Z, X), 20))});
        c.loops = {circleWire({20, 0, 0}, Y, 2)};
        c.expected = 2 * pi * pi * 20 * 4;
        c.formula = "2 pi^2 R r^2";
    } else if (name == "polyline") {       // (0,0,0)->(50,0,0)->(50,50,0), r 2
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), line({50, 0, 0}, {50, 50, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
        c.expected = 4 * pi * 100;
        c.formula = "A L (mitred corner, centred profile)";
    } else if (name == "square") {         // closed square 50 mm, starting mid-side, r 2
        c.loops = {circleWire({25, 0, 0}, X, 2)};
        c.spine = wire({line({25, 0, 0}, {50, 0, 0}), line({50, 0, 0}, {50, 50, 0}), line({50, 50, 0}, {0, 50, 0}),
                        line({0, 50, 0}, {0, 0, 0}), line({0, 0, 0}, {25, 0, 0})});
        c.expected = 4 * pi * 200;
        c.formula = "A L (mitred corners)";
    } else if (name == "line-arc-line") {  // 50 + quarter R 20 + 50, r 2
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), arc({50, 20, 0}, Z, {50, 0, 0}, {70, 20, 0}),
                        line({70, 20, 0}, {70, 70, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
        c.expected = 4 * pi * (100 + 10 * pi);
        c.formula = "A L, L = 100 + 10 pi";
    } else if (name == "rect-arc") {       // 4 x 2 rectangle along a 90 deg arc, R 50 (Pappus)
        c.spine = wire({arc({0, 0, 0}, Z, {50, 0, 0}, {0, 50, 0})});
        c.loops = {rectWire({50, 0, 0}, Y, X, 4, 2)};
        c.expected = 8 * 50 * pi / 2;
        c.formula = "A R theta (Pappus)";
    } else if (name == "annulus-arc") {    // tube r 3 / 2 along the quarter arc R 20
        c.spine = wire({arc({0, 0, 0}, Z, {20, 0, 0}, {0, 20, 0})});
        c.loops = {circleWire({20, 0, 0}, Y, 3), circleWire({20, 0, 0}, Y, 2)};
        c.expected = pi * 5 * 20 * pi / 2;
        c.formula = "pi (3^2 - 2^2) R theta";
    } else if (name == "arc270") {         // r 2 along a 270 deg arc, R 20
        c.spine = wire({arc({0, 0, 0}, Z, {20, 0, 0}, {0, -20, 0})});
        c.loops = {circleWire({20, 0, 0}, Y, 2)};
        c.expected = 4 * pi * 20 * 1.5 * pi;
        c.formula = "pi r^2 R 3pi/2";
    } else if (name == "self-intersect") { // r 25 along the quarter arc R 20: crosses the axis
        c.spine = wire({arc({0, 0, 0}, Z, {20, 0, 0}, {0, 20, 0})});
        c.loops = {circleWire({20, 0, 0}, Y, 25)};
    } else if (name == "tight-corner") {   // r 10 around a polyline corner with 5 mm legs
        c.spine = wire({line({0, 0, 0}, {5, 0, 0}), line({5, 0, 0}, {5, 5, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 10)};
    } else if (name == "offset-profile") { // circle r 2 centred 10 mm off the straight path start
        c.spine = wire({line({0, 0, 0}, {0, 0, 100})});
        c.loops = {circleWire({10, 0, 0}, Z, 2)};
        c.binormal = X;
        c.expected = 400 * pi;
        c.formula = "A L";
    } else if (name == "offset-quarter") { // circle r 2 centred 5 mm outside the arc R 20 (Pappus R 25)
        c.spine = wire({arc({0, 0, 0}, Z, {20, 0, 0}, {0, 20, 0})});
        c.loops = {circleWire({25, 0, 0}, Y, 2)};
        c.expected = 4 * pi * 25 * pi / 2;
        c.formula = "A (R+5) theta";
    } else if (name == "crossing") {       // line, 270 deg tangent arc R 10, line back across the first line
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), arc({50, 10, 0}, Z, {50, 0, 0}, {40, 10, 0}),
                        line({40, 10, 0}, {40, -30, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
    } else if (name == "u-close") {        // U: legs 3 mm apart around a tangent half circle R 1.5, r 2
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), arc({50, 1.5, 0}, Z, {50, 0, 0}, {50, 3, 0}),
                        line({50, 3, 0}, {0, 3, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 1)};
    } else if (name == "u-apart") {        // the same U with legs 20 mm apart (R 10): no overlap
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), arc({50, 10, 0}, Z, {50, 0, 0}, {50, 20, 0}),
                        line({50, 20, 0}, {0, 20, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
        c.expected = 4 * pi * (100 + 10 * pi);
        c.formula = "A L";
    } else if (name == "square-vertex") {  // closed square starting at a corner
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), line({50, 0, 0}, {50, 50, 0}), line({50, 50, 0}, {0, 50, 0}),
                        line({0, 50, 0}, {0, 0, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
        c.expected = 4 * pi * 200;
        c.formula = "A L (mitred corners)";
    } else if (name == "line-arc-corner") { // a line meeting an arc at 90 deg (not tangent), r 2
        // Quarter arc about (70, 0, 0), clockwise about Z: it leaves (50, 0, 0) along +Y.
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), arc({70, 0, 0}, Z.Reversed(), {50, 0, 0}, {70, 20, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
    } else if (name == "reversed") {       // straight path from the profile towards -Z
        c.spine = wire({line({0, 0, 0}, {0, 0, -100})});
        c.loops = {rectWire({0, 0, 0}, Z, X, 10, 20)};
        c.binormal = X;
        c.expected = 20000;
        c.formula = "A L";
    } else if (name == "hairpin") {        // two lines turning back by 180 deg
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), line({50, 0, 0}, {0, 0, 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
    } else if (name == "obtuse") {         // polyline turning by 135 deg, legs 50, r 2
        c.spine = wire({line({0, 0, 0}, {50, 0, 0}), line({50, 0, 0}, {50 - 50 / std::sqrt(2.0), 50 / std::sqrt(2.0), 0})});
        c.loops = {circleWire({0, 0, 0}, X, 2)};
        c.expected = 4 * pi * 100;
        c.formula = "A L (mitred corner)";
    } else {
        return false;
    }
    return true;
}

void report(const TopoDS_Shape& shape, const Case& c, double seconds) {
    int solids = 0;
    for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next()) {
        ++solids;
    }
    std::map<std::string, int> surfaces;
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
        const char* names[] = {"plane", "cylinder", "cone", "sphere", "torus", "bezier", "bspline",
                               "revolution", "extrusion", "offset", "other"};
        const int type = BRepAdaptor_Surface(TopoDS::Face(e.Current())).GetType();
        ++surfaces[names[type < 11 ? type : 10]];
    }
    const bool valid = BRepCheck_Analyzer(shape).IsValid();
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const gp_Pnt g = props.CentreOfMass();
    std::printf("done solids=%d valid=%d V=%.17g", solids, valid ? 1 : 0, props.Mass());
    if (c.expected != 0.0) {
        std::printf(" expected=%.17g (%s) rel=%.2e", c.expected, c.formula,
                    std::abs(props.Mass() - c.expected) / c.expected);
    }
    std::printf(" box=(%.9g,%.9g,%.9g)-(%.9g,%.9g,%.9g) centre=(%.9g,%.9g,%.9g) t=%.3fs faces:", x0, y0, z0, x1, y1,
                z1, g.X(), g.Y(), g.Z(), seconds);
    for (const auto& [n, k] : surfaces) {
        std::printf(" %s=%d", n.c_str(), k);
    }
    // OCCT's argument analyzer: self-interference and small edges.
    const auto t0 = std::chrono::steady_clock::now();
    BRepAlgoAPI_Check check(shape, /*bTestSE=*/false, /*bTestSI=*/true);
    const double tc = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf(" selfcheck=%d (%.3fs)\n", check.IsValid() ? 1 : 0, tc);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::printf("usage: %s <case> <pipe|pipe-cn|shell-t|shell-r|shell-o>\n", argv[0]);
        return 2;
    }
    Case c;
    if (!makeCase(argv[1], c)) {
        std::printf("unknown case\n");
        return 2;
    }
    const std::string builder = argv[2];
    std::printf("%-16s %-8s ", argv[1], argv[2]);
    std::fflush(stdout);
    try {
        const auto t0 = std::chrono::steady_clock::now();
        TopoDS_Shape result;
        if (builder == "pipe" || builder == "pipe-cn") {
            // A face with its holes, swept along the spine.
            BRepBuilderAPI_MakeFace face(c.loops[0], true);
            for (std::size_t i = 1; i < c.loops.size(); ++i) {
                face.Add(TopoDS::Wire(c.loops[i].Reversed()));
            }
            BRepOffsetAPI_MakePipe pipe(c.spine, face.Face(),
                                        builder == "pipe" ? GeomFill_IsCorrectedFrenet : GeomFill_IsConstantNormal);
            pipe.Build();
            if (!pipe.IsDone()) {
                std::printf("not done\n");
                return 1;
            }
            result = pipe.Shape();
        } else if (builder == "shell-rh") {
            // Every loop swept on its own (binormal mode, mitred corners); holes subtracted.
            const auto sweepLoop = [&](const TopoDS_Wire& loop) -> TopoDS_Shape {
                BRepOffsetAPI_MakePipeShell shell(c.spine);
                shell.SetMode(c.binormal);
                shell.SetTransitionMode(BRepBuilderAPI_RightCorner);
                shell.Add(loop, false, false);
                shell.Build();
                if (!shell.IsDone() || !shell.MakeSolid()) {
                    return {};
                }
                return shell.Shape();
            };
            result = sweepLoop(c.loops[0]);
            for (std::size_t i = 1; i < c.loops.size() && !result.IsNull(); ++i) {
                const TopoDS_Shape hole = sweepLoop(c.loops[i]);
                result = hole.IsNull() ? TopoDS_Shape{} : BRepAlgoAPI_Cut(result, hole).Shape();
            }
            if (result.IsNull()) {
                std::printf("not done\n");
                return 1;
            }
        } else {
            BRepOffsetAPI_MakePipeShell shell(c.spine);
            shell.SetMode(c.binormal);
            shell.SetTransitionMode(builder == "shell-t"   ? BRepBuilderAPI_Transformed
                                    : builder == "shell-r" ? BRepBuilderAPI_RightCorner
                                                           : BRepBuilderAPI_RoundCorner);
            shell.Add(c.loops[0], false, false);
            shell.Build();
            if (!shell.IsDone()) {
                std::printf("not done\n");
                return 1;
            }
            if (!shell.MakeSolid()) {
                std::printf("no solid\n");
                return 1;
            }
            result = shell.Shape();
        }
        const auto t1 = std::chrono::steady_clock::now();
        report(result, c, std::chrono::duration<double>(t1 - t0).count());
    } catch (const Standard_Failure& e) {
        std::printf("Standard_Failure: %s\n", e.what());
        return 1;
    } catch (...) {
        std::printf("unknown exception\n");
        return 1;
    }
    return 0;
}
