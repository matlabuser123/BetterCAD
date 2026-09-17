// Kernel probe (evidence, not part of the build): how many faces the kernel
// generates from one straight edge filleted with radius stations, for edges
// from 1 to 2000 mm long and 2 to 25 stations (radii rising evenly from 2 to
// 12 mm, 0.1 to 0.4 mm on the 1 mm edge). geometry::variableFilletEdges()
// requires exactly one fillet face per edge; this probe looked for a case
// where the kernel splits it.
//
// Built like the other probes (see station-contract-probe.log), without -Wall.
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <NCollection_Array1.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt2d.hxx>
#include <cstdio>
#include <cmath>
#include <vector>
#include <utility>
int main() {
    for (const double length : {1.0, 10.0, 100.0, 2000.0}) {
        for (const int n : {2, 5, 12, 25}) {
            const TopoDS_Shape box = BRepPrimAPI_MakeBox(length, 60.0, 40.0).Shape();
            TopoDS_Edge edge;
            for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
                const TopoDS_Edge e = TopoDS::Edge(it.Current());
                TopoDS_Vertex a, b; TopExp::Vertices(e, a, b);
                if (BRep_Tool::Pnt(a).Distance(gp_Pnt(0,0,0)) < 1e-9 && BRep_Tool::Pnt(b).Distance(gp_Pnt(length,0,0)) < 1e-9) edge = e;
            }
            BRepFilletAPI_MakeFillet maker(box);
            maker.Add(edge);
            NCollection_Array1<gp_Pnt2d> uandr(1, n);
            double rmax = length < 5 ? 0.4 : 12.0;
            double rmin = length < 5 ? 0.1 : 2.0;
            for (int i = 0; i < n; ++i) {
                double u = double(i) / (n - 1);
                uandr.SetValue(i + 1, gp_Pnt2d(u, rmin + (rmax - rmin) * u));
            }
            maker.SetRadius(uandr, maker.Contour(edge), 1);
            maker.Build();
            int faces = 0;
            if (maker.IsDone()) {
                for (const auto& g : maker.Generated(edge)) if (g.ShapeType() == TopAbs_FACE) ++faces;
            }
            std::printf("length %6.0f stations %2d: done %d surfaces %d generated faces %d valid %d\n", length, n,
                        maker.IsDone() ? 1 : 0, maker.IsDone() ? maker.NbSurfaces() : -1, faces,
                        maker.IsDone() ? (BRepCheck_Analyzer(maker.Shape()).IsValid() ? 1 : 0) : -1);
        }
    }
}
