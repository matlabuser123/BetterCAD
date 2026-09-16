// Kernel probe (evidence, not part of the build): per-face volume
// contributions and their cost under the kernel's integrators, for the solid
// kinds whose mass properties P12-SKETCH-002 moved to Gauss-Kronrod
// integration. It showed which integration is exact and fast for each
// surface type, which Body::massProperties() follows since P12-DATUM-001.
// (The kernel integrates a single face about the mean of its vertex
// occurrences, which lies in a planar face's plane: planar faces show 0
// here, and the sums are not the solids' volumes.)
//
// For every face: its surface type, and its volume contribution
// (1/3 of the flux of the position through it) and time with
//   ada    BRepGProp::VolumeProperties(face, eps 1e-10) (adaptive Gauss),
//   gk10   BRepGProp::VolumePropertiesGK(face, eps 1e-10, spans),
//   gk6    the same at eps 1e-6.
// Per solid: the sums and their errors against the analytic volume.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       face_integration_probe.cpp -L<deps>/lib -lTKOffset -lTKPrim
//       -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d -lTKG3d -lTKMath
//       -lTKernel
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_Array1.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <string>

namespace {

const double pi = std::numbers::pi;

using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

TopoDS_Face planarFace(const TopoDS_Wire& wire, const gp_Pnt& origin = gp_Pnt(0, 0, 0)) {
    return BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(origin, gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))), wire, true).Face();
}

TopoDS_Wire wireOf(const TopoDS_Edge& edge) {
    return BRepBuilderAPI_MakeWire(edge).Wire();
}

TopoDS_Edge periodicSquare(double x0, double y0, double side) {
    NCollection_Array1<gp_Pnt> poles(1, 4);
    poles.SetValue(1, gp_Pnt(x0, y0, 0));
    poles.SetValue(2, gp_Pnt(x0 + side, y0, 0));
    poles.SetValue(3, gp_Pnt(x0 + side, y0 + side, 0));
    poles.SetValue(4, gp_Pnt(x0, y0 + side, 0));
    NCollection_Array1<double> knots(1, 5);
    NCollection_Array1<int> mults(1, 5);
    for (int i = 1; i <= 5; ++i) {
        knots.SetValue(i, i - 1.0);
        mults.SetValue(i, 1);
    }
    return BRepBuilderAPI_MakeEdge(occ::handle<Geom_BSplineCurve>(
                                       new Geom_BSplineCurve(poles, knots, mults, 2, true)))
        .Edge();
}

void measure(const std::string& name, const TopoDS_Shape& solid, double expected) {
    std::printf("%s (V = %.17g)\n", name.c_str(), expected);
    double ada = 0.0;
    double gk10 = 0.0;
    double gk6 = 0.0;
    double tAda = 0.0;
    double tGk10 = 0.0;
    double tGk6 = 0.0;
    int index = 0;
    for (TopExp_Explorer it(solid, TopAbs_FACE); it.More(); it.Next(), ++index) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        const BRepAdaptor_Surface surface(face, false);
        GProp_GProps a;
        GProp_GProps b;
        GProp_GProps c;
        const auto t0 = Clock::now();
        BRepGProp::VolumeProperties(face, a, 1e-10, false);
        const auto t1 = Clock::now();
        BRepGProp::VolumePropertiesGK(face, b, 1e-10, false, true, true);
        const auto t2 = Clock::now();
        BRepGProp::VolumePropertiesGK(face, c, 1e-6, false, true, true);
        const auto t3 = Clock::now();
        std::printf("  face %2d type %2d  ada %22.17g %.3fs  gk10 %22.17g %.3fs  gk6 %22.17g %.3fs\n", index,
                    static_cast<int>(surface.GetType()), a.Mass(), seconds(t0, t1), b.Mass(), seconds(t1, t2),
                    c.Mass(), seconds(t2, t3));
        ada += a.Mass();
        gk10 += b.Mass();
        gk6 += c.Mass();
        tAda += seconds(t0, t1);
        tGk10 += seconds(t1, t2);
        tGk6 += seconds(t2, t3);
    }
    std::printf("  sums: ada rel %.2e (%.3fs)  gk10 rel %.2e (%.3fs)  gk6 rel %.2e (%.3fs)\n\n",
                std::abs(ada / expected - 1), tAda, std::abs(gk10 / expected - 1), tGk10,
                std::abs(gk6 / expected - 1), tGk6);
}

} // namespace

int main() {
    std::printf("P12-DATUM-001 face integration probe (OCCT 8.0.1)\n");
    std::printf("Surface types: 0 plane, 1 cylinder, 2 cone, 3 sphere, 4 torus, 5 Bezier, 6 B-spline,\n");
    std::printf("7 surface of revolution, 8 surface of extrusion, 9 offset, 10 other.\n\n");
    const gp_Ax1 yAxis(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));

    const gp_Elips wide(gp_Ax2(gp_Pnt(5, -7, 0), gp_Dir(0, 0, 1), gp_Dir(std::cos(pi / 6), std::sin(pi / 6), 0)),
                        30.0, 12.0);
    measure("elliptic prism",
            BRepPrimAPI_MakePrism(planarFace(wireOf(BRepBuilderAPI_MakeEdge(wide).Edge())), gp_Vec(0, 0, 25)).Shape(),
            pi * 360 * 25);
    measure("spline square prism",
            BRepPrimAPI_MakePrism(planarFace(wireOf(periodicSquare(0, 0, 60))), gp_Vec(0, 0, 10)).Shape(), 30000);
    const gp_Elips ring(gp_Ax2(gp_Pnt(50, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 10.0, 6.0);
    measure("revolved ellipse",
            BRepPrimAPI_MakeRevol(planarFace(wireOf(BRepBuilderAPI_MakeEdge(ring).Edge())), yAxis).Shape(),
            2 * pi * 50 * pi * 60);
    measure("revolved spline square",
            BRepPrimAPI_MakeRevol(planarFace(wireOf(periodicSquare(40, 20, 20))), yAxis).Shape(),
            2 * pi * 50 * (5.0 * 400 / 6));

    // A ruled loft between squares 20 and 10 mm, 30 mm apart (a frustum):
    // V = h/3 (A0 + A1 + sqrt(A0 A1)).
    BRepOffsetAPI_ThruSections loft(true, true);
    loft.CheckCompatibility(false);
    loft.AddWire(BRepBuilderAPI_MakePolygon(gp_Pnt(-10, -10, 0), gp_Pnt(10, -10, 0), gp_Pnt(10, 10, 0),
                                            gp_Pnt(-10, 10, 0), true)
                     .Wire());
    loft.AddWire(BRepBuilderAPI_MakePolygon(gp_Pnt(-5, -5, 30), gp_Pnt(5, -5, 30), gp_Pnt(5, 5, 30),
                                            gp_Pnt(-5, 5, 30), true)
                     .Wire());
    loft.Build();
    measure("ruled square frustum loft", loft.Shape(), 30.0 / 3.0 * (400.0 + 100.0 + 200.0));

    BRepOffsetAPI_ThruSections cone(true, true);
    cone.CheckCompatibility(false);
    cone.AddWire(wireOf(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 15)).Edge()));
    cone.AddWire(wireOf(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(3, 0, 25), gp_Dir(0, 0, 1)), 5)).Edge()));
    cone.Build();
    measure("ruled oblique circle loft", cone.Shape(), pi * 25.0 / 3.0 * (225.0 + 25.0 + 75.0));

    // An ellipse swept up 40 mm and round a quarter turn of radius 60.
    BRepBuilderAPI_MakeWire spine;
    spine.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(0, 0, 40)).Edge());
    spine.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(gp_Pnt(0, 0, 40),
                                                         gp_Pnt(60 - 60 * std::cos(pi / 4), 0,
                                                                40 + 60 * std::sin(pi / 4)),
                                                         gp_Pnt(60, 0, 100))
                                          .Value())
                  .Edge());
    const gp_Elips section(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 10.0, 6.0);
    BRepOffsetAPI_MakePipeShell pipe(spine.Wire());
    pipe.SetMode(gp_Dir(0, -1, 0));
    pipe.SetTransitionMode(BRepBuilderAPI_RightCorner);
    pipe.Add(wireOf(BRepBuilderAPI_MakeEdge(section).Edge()), false, false);
    pipe.Build();
    pipe.MakeSolid();
    measure("swept ellipse", pipe.Shape(), pi * 60.0 * (40.0 + 30.0 * pi));
    return 0;
}
