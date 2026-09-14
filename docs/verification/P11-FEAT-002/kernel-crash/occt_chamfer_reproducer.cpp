// Stand-alone Open CASCADE reproducer for the chamfer crash found while
// qualifying P11-FEAT-002. It uses no BetterCAD code. Not part of the build.
//
// Build (GCC 16.1 MinGW UCRT, the OCCT 8.0.1 of the dependency superbuild):
//   c++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade \
//       occt_chamfer_reproducer.cpp -o occt_chamfer_reproducer.exe -L<deps>/lib \
//       -lTKFillet -lTKPrim -lTKTopAlgo -lTKBRep -lTKGeomBase -lTKG3d -lTKG2d -lTKMath -lTKernel
//
// Usage:
//   occt_chamfer_reproducer chamfer <d>   chamfer the top front edge of a
//                                         100 x 50 x 20 mm box by d mm
//   occt_chamfer_reproducer pair <d>      chamfer both long front edges of a
//                                         100 x 50 x 2 mm plate, 1 mm and d mm
//   occt_chamfer_reproducer exception     a StdFail_NotDone thrown in TKTopAlgo
//   occt_chamfer_reproducer no-edges      a Standard_Failure thrown in TKFillet
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

/// Adds the edges along X on the front face (y = 0) at height z = h (distance
/// dTop) and, if dBottom > 0, at z = 0 (distance dBottom).
void addFrontEdges(BRepFilletAPI_MakeChamfer& maker, const TopoDS_Shape& box, double h, double dTop, double dBottom) {
    for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        const gp_Pnt a = BRep_Tool::Pnt(TopExp::FirstVertex(edge));
        const gp_Pnt b = BRep_Tool::Pnt(TopExp::LastVertex(edge));
        const bool alongFront = std::abs(a.Y()) < 1e-9 && std::abs(b.Y()) < 1e-9 && std::abs(a.Z() - b.Z()) < 1e-9;
        if (alongFront && std::abs(a.Z() - h) < 1e-9) {
            maker.Add(dTop, edge);
        } else if (alongFront && dBottom > 0.0 && std::abs(a.Z()) < 1e-9) {
            maker.Add(dBottom, edge);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "chamfer";
    const double d = argc > 2 ? std::atof(argv[2]) : 25.0;
    try {
        if (mode == "chamfer" || mode == "pair") {
            const double h = mode == "chamfer" ? 20.0 : 2.0;
            const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 50.0, h).Shape();
            BRepFilletAPI_MakeChamfer maker(box);
            addFrontEdges(maker, box, h, mode == "chamfer" ? d : 1.0, mode == "chamfer" ? 0.0 : d);
            maker.Build();
            std::printf("%s %.10g: IsDone %d\n", mode.c_str(), d, maker.IsDone() ? 1 : 0);
        } else if (mode == "exception") {
            BRepBuilderAPI_MakeEdge edge(gp_Pnt(0, 0, 0), gp_Pnt(0, 0, 0));
            std::printf("exception: none, null edge %d\n", edge.Edge().IsNull() ? 1 : 0);
        } else if (mode == "no-edges") {
            const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 50.0, 20.0).Shape();
            BRepFilletAPI_MakeChamfer maker(box);
            std::printf("no-edges: none, null shape %d\n", maker.Shape().IsNull() ? 1 : 0);
        }
    } catch (const Standard_Failure& failure) {
        std::printf("%s: caught %s: %s\n", mode.c_str(), failure.ExceptionType(), failure.what());
    }
    return 0;
}
