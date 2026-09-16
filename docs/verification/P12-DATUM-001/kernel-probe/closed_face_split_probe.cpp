// Kernel probe (evidence, not part of the build): whether splitting closed
// faces (ShapeUpgrade_ShapeDivideClosed, 1, 2 and 4 split points) makes the
// kernel's Gauss-Kronrod volume integration (spans on, centre of gravity on)
// fast on the solids where it took seconds. It does not; P12-DATUM-001 did
// not take this route.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       closed_face_split_probe.cpp -L<deps>/lib -lTKShHealing -lTKOffset
//       -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d
//       -lTKG3d -lTKMath -lTKernel
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_Array1.hxx>
#include <ShapeUpgrade_ShapeDivideClosed.hxx>
#include <TopExp_Explorer.hxx>
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
#include <numbers>
#include <string>

namespace {

const double pi = std::numbers::pi;

using Clock = std::chrono::steady_clock;

TopoDS_Face planarFace(const TopoDS_Wire& wire) {
    return BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))), wire, true)
        .Face();
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

void measure(const std::string& name, const TopoDS_Shape& shape, double expected) {
    for (const int splitPoints : {0, 1, 2, 4}) {
        TopoDS_Shape divided = shape;
        if (splitPoints > 0) {
            ShapeUpgrade_ShapeDivideClosed divide(shape);
            divide.SetNbSplitPoints(splitPoints);
            divide.Perform();
            divided = divide.Result();
        }
        int faces = 0;
        for (TopExp_Explorer it(divided, TopAbs_FACE); it.More(); it.Next()) {
            ++faces;
        }
        for (const double eps : {1e-6, 1e-10}) {
            const auto t0 = Clock::now();
            GProp_GProps props;
            const double reported = BRepGProp::VolumePropertiesGK(divided, props, eps, true, true, true);
            const auto t1 = Clock::now();
            std::printf("%-22s split %d (%2d faces)  eps %.0e  rel %.2e  reported %.2e  %.3f s\n", name.c_str(),
                        splitPoints, faces, eps, std::abs(props.Mass() / expected - 1), reported,
                        std::chrono::duration<double>(t1 - t0).count());
        }
    }
}

} // namespace

int main() {
    std::printf("P12-DATUM-001 closed face split probe (OCCT 8.0.1)\n\n");
    const gp_Ax1 yAxis(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
    const gp_Elips ring(gp_Ax2(gp_Pnt(50, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 10.0, 6.0);
    measure("revolved ellipse",
            BRepPrimAPI_MakeRevol(planarFace(wireOf(BRepBuilderAPI_MakeEdge(ring).Edge())), yAxis).Shape(),
            2 * pi * 50 * pi * 60);
    measure("revolved spline",
            BRepPrimAPI_MakeRevol(planarFace(wireOf(periodicSquare(40, 20, 20))), yAxis).Shape(),
            2 * pi * 50 * (5.0 * 400 / 6));

    BRepOffsetAPI_ThruSections cone(true, true);
    cone.CheckCompatibility(false);
    cone.AddWire(wireOf(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 15)).Edge()));
    cone.AddWire(wireOf(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(3, 0, 25), gp_Dir(0, 0, 1)), 5)).Edge()));
    cone.Build();
    const double coneVolume = pi * 25.0 / 3.0 * (225.0 + 25.0 + 75.0);
    measure("oblique circle loft", cone.Shape(), coneVolume);

    const auto t0 = Clock::now();
    GProp_GProps props;
    BRepGProp::VolumeProperties(cone.Shape(), props, 1e-10, true);
    const auto t1 = Clock::now();
    std::printf("oblique circle loft, adaptive Gauss eps 1e-10: rel %.2e  %.3f s\n",
                std::abs(props.Mass() / coneVolume - 1), std::chrono::duration<double>(t1 - t0).count());
    return 0;
}
