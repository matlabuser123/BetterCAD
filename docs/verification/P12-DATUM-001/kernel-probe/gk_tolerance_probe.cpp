// Kernel probe (evidence, not part of the build): accuracy and time of the
// kernel's Gauss-Kronrod volume integration (BRepGProp::VolumePropertiesGK,
// spans on, centre of gravity on) against its tolerance, on the curved
// solids P12-SKETCH-002 introduced. It found the ~10 s the whole-body
// integration of P12-SKETCH-002 took on full revolutions, at every tolerance
// tight enough to be exact, which P12-DATUM-001 removed.
//
//   gk_tolerance_probe
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       gk_tolerance_probe.cpp -L<deps>/lib -lTKPrim -lTKTopAlgo -lTKGeomAlgo
//       -lTKBRep -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_Array1.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

namespace {

const double pi = std::numbers::pi;

TopoDS_Face face(const TopoDS_Edge& edge) {
    BRepBuilderAPI_MakeWire wire(edge);
    return BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))), wire.Wire(),
                                   true)
        .Face();
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
    const occ::handle<Geom_BSplineCurve> curve = new Geom_BSplineCurve(poles, knots, mults, 2, true);
    return BRepBuilderAPI_MakeEdge(curve).Edge();
}

void measure(const std::string& name, const TopoDS_Shape& solid, double expected) {
    for (const double eps : {1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-10}) {
        const auto t0 = std::chrono::steady_clock::now();
        GProp_GProps v;
        const double reported =
            BRepGProp::VolumePropertiesGK(solid, v, eps, /*OnlyClosed=*/true, /*IsUseSpan=*/true, /*CGFlag=*/true);
        const auto t1 = std::chrono::steady_clock::now();
        std::printf("%-26s eps %.0e  V %.17g  rel %.2e  reported %.2e  %.3f s\n", name.c_str(), eps, v.Mass(),
                    std::abs(v.Mass() / expected - 1), reported, std::chrono::duration<double>(t1 - t0).count());
    }
}

} // namespace

int main() {
    std::printf("P12-DATUM-001 Gauss-Kronrod tolerance probe (OCCT 8.0.1)\n\n");
    const gp_Ax1 yAxis(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));

    const gp_Elips ring(gp_Ax2(gp_Pnt(50, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 10.0, 6.0);
    measure("revolved ellipse", BRepPrimAPI_MakeRevol(face(BRepBuilderAPI_MakeEdge(ring).Edge()), yAxis).Shape(),
            2 * pi * 50 * pi * 60);
    measure("revolved ellipse 90 deg",
            BRepPrimAPI_MakeRevol(face(BRepBuilderAPI_MakeEdge(ring).Edge()), yAxis, pi / 2).Shape(),
            2 * pi * 50 * pi * 60 / 4);
    measure("revolved spline square",
            BRepPrimAPI_MakeRevol(face(periodicSquare(40, 20, 20)), yAxis).Shape(), 2 * pi * 50 * (5.0 * 400 / 6));

    const gp_Elips wide(gp_Ax2(gp_Pnt(5, -7, 0), gp_Dir(0, 0, 1), gp_Dir(std::cos(pi / 6), std::sin(pi / 6), 0)),
                        30.0, 12.0);
    measure("elliptic prism",
            BRepPrimAPI_MakePrism(face(BRepBuilderAPI_MakeEdge(wide).Edge()), gp_Vec(0, 0, 25)).Shape(),
            pi * 360 * 25);
    measure("spline square prism", BRepPrimAPI_MakePrism(face(periodicSquare(0, 0, 60)), gp_Vec(0, 0, 10)).Shape(),
            30000);
    return 0;
}
