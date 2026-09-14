// Stand-alone Open CASCADE reproducer for the fillet behaviour measured while
// qualifying P11-FEAT-003: feasible radii build (with volumes matching the
// closed forms in README.md), radii that do not fit crash the process. It
// uses no BetterCAD code. Not part of the build.
//
// Build (GCC 16.1 MinGW UCRT, the OCCT 8.0.1 of the dependency superbuild):
//   c++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade \
//       occt_fillet_reproducer.cpp -o occt_fillet_reproducer.exe -L<deps>/lib \
//       -lTKFillet -lTKPrim -lTKTopAlgo -lTKBRep -lTKGeomBase -lTKGeomAlgo -lTKG3d -lTKG2d -lTKMath -lTKernel
//
// Usage (radii in mm):
//   occt_fillet_reproducer box <r>            top front edge of a 100 x 50 x 20 box
//   occt_fillet_reproducer pair <r2>          front edges of a 100 x 50 x 2 plate, 1 and r2
//   occt_fillet_reproducer opposite <r>       top front + top back edges of a 100 x 50 x 30 box
//   occt_fillet_reproducer corner2 <r>        top front + top left edges (meet at a vertex)
//   occt_fillet_reproducer corner3 <r>        top front + top left + front left (vertical) edges
//   occt_fillet_reproducer lshape <r>         concave vertical edge of an L-shaped prism
//   occt_fillet_reproducer rim <r>            top rim of a cylinder r = 15, h = 40
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace {

bool near(double a, double b) {
    return std::abs(a - b) < 1e-7;
}

/// Adds every straight edge whose end points both satisfy @p test.
int addEdges(BRepFilletAPI_MakeFillet& maker, const TopoDS_Shape& shape, double r,
             const std::function<bool(const gp_Pnt&, const gp_Pnt&)>& test) {
    int added = 0;
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        if (BRep_Tool::Degenerated(edge)) {
            continue;
        }
        const gp_Pnt a = BRep_Tool::Pnt(TopExp::FirstVertex(edge));
        const gp_Pnt b = BRep_Tool::Pnt(TopExp::LastVertex(edge));
        if (test(a, b) && maker.Contour(edge) == 0) {
            maker.Add(r, edge);
            ++added;
        }
    }
    return added;
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "box";
    const double r = argc > 2 ? std::atof(argv[2]) : 5.0;
    try {
        TopoDS_Shape shape;
        std::function<int(BRepFilletAPI_MakeFillet&)> select;
        if (mode == "box" || mode == "corner2" || mode == "corner3") {
            shape = BRepPrimAPI_MakeBox(100.0, 50.0, 20.0).Shape();
            const auto topFront = [](const gp_Pnt& a, const gp_Pnt& b) {
                return near(a.Y(), 0) && near(b.Y(), 0) && near(a.Z(), 20) && near(b.Z(), 20);
            };
            const auto topLeft = [](const gp_Pnt& a, const gp_Pnt& b) {
                return near(a.X(), 0) && near(b.X(), 0) && near(a.Z(), 20) && near(b.Z(), 20);
            };
            const auto frontLeft = [](const gp_Pnt& a, const gp_Pnt& b) {
                return near(a.X(), 0) && near(b.X(), 0) && near(a.Y(), 0) && near(b.Y(), 0);
            };
            select = [&, topFront, topLeft, frontLeft](BRepFilletAPI_MakeFillet& m) {
                int n = addEdges(m, shape, r, topFront);
                if (mode != "box") n += addEdges(m, shape, r, topLeft);
                if (mode == "corner3") n += addEdges(m, shape, r, frontLeft);
                return n;
            };
        } else if (mode == "pair") {
            shape = BRepPrimAPI_MakeBox(100.0, 50.0, 2.0).Shape();
            select = [&](BRepFilletAPI_MakeFillet& m) {
                int n = addEdges(m, shape, 1.0, [](const gp_Pnt& a, const gp_Pnt& b) {
                    return near(a.Y(), 0) && near(b.Y(), 0) && near(a.Z(), 2) && near(b.Z(), 2);
                });
                return n + addEdges(m, shape, r, [](const gp_Pnt& a, const gp_Pnt& b) {
                    return near(a.Y(), 0) && near(b.Y(), 0) && near(a.Z(), 0) && near(b.Z(), 0);
                });
            };
        } else if (mode == "opposite") {
            shape = BRepPrimAPI_MakeBox(100.0, 50.0, 30.0).Shape();
            select = [&](BRepFilletAPI_MakeFillet& m) {
                return addEdges(m, shape, r, [](const gp_Pnt& a, const gp_Pnt& b) {
                    return near(a.Z(), 30) && near(b.Z(), 30) && near(a.Y(), b.Y()) && (near(a.Y(), 0) || near(a.Y(), 50));
                });
            };
        } else if (mode == "lshape") {
            // L: 50 x 50 square minus the 30 x 30 square at its +X+Y corner, 20 high.
            BRepBuilderAPI_MakePolygon polygon;
            for (const auto& [x, y] : {std::pair{0.0, 0.0}, {50.0, 0.0}, {50.0, 20.0}, {20.0, 20.0}, {20.0, 50.0}, {0.0, 50.0}}) {
                polygon.Add(gp_Pnt(x, y, 0.0));
            }
            polygon.Close();
            const TopoDS_Face base = BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
            shape = BRepPrimAPI_MakePrism(base, gp_Vec(0, 0, 20)).Shape();
            select = [&](BRepFilletAPI_MakeFillet& m) {
                return addEdges(m, shape, r, [](const gp_Pnt& a, const gp_Pnt& b) {
                    return near(a.X(), 20) && near(b.X(), 20) && near(a.Y(), 20) && near(b.Y(), 20);
                });
            };
        } else if (mode == "rim") {
            shape = BRepPrimAPI_MakeCylinder(15.0, 40.0).Shape();
            select = [&](BRepFilletAPI_MakeFillet& m) {
                int n = 0;
                for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
                    const TopoDS_Edge edge = TopoDS::Edge(it.Current());
                    const gp_Pnt a = BRep_Tool::Pnt(TopExp::FirstVertex(edge));
                    const gp_Pnt b = BRep_Tool::Pnt(TopExp::LastVertex(edge));
                    if (a.IsEqual(b, 1e-7) && near(a.Z(), 40) && m.Contour(edge) == 0) { // the closed top circle
                        m.Add(r, edge);
                        ++n;
                    }
                }
                return n;
            };
        }
        GProp_GProps before;
        BRepGProp::VolumeProperties(shape, before);
        BRepFilletAPI_MakeFillet maker(shape);
        const int n = select(maker);
        maker.Build();
        if (!maker.IsDone()) {
            std::printf("%s %.10g: %d edge(s), IsDone 0\n", mode.c_str(), r, n);
            return 0;
        }
        GProp_GProps after;
        BRepGProp::VolumeProperties(maker.Shape(), after);
        std::printf("%s %.10g: %d edge(s), IsDone 1, valid %d, V0 %.17g, V %.17g, removed %.17g\n", mode.c_str(), r, n,
                    BRepCheck_Analyzer(maker.Shape()).IsValid() ? 1 : 0, before.Mass(), after.Mass(),
                    before.Mass() - after.Mass());
    } catch (const Standard_Failure& failure) {
        std::printf("%s %.10g: caught %s: %s\n", mode.c_str(), r, failure.ExceptionType(), failure.what());
    }
    return 0;
}
