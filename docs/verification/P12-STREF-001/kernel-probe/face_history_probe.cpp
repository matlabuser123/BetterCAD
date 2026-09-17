// Kernel probe (evidence, not part of the build): the face history OCCT 8.0.1
// reports for the operations P12-STREF-001 carries face names through.
//
//  A. BRepPrimAPI_MakePrism: are the wire's edges the profile face's edges
//     (IsSame), and does Generated(edge) give one side face each?
//     FirstShape()/LastShape(): the caps at the start and end of the sweep.
//  B. Booleans as BetterCAD runs them (BRepAlgoAPI_Fuse/Cut/Common, not
//     parallel, SimplifyResult()): for every face of both operands,
//     IsDeleted(), Modified() and whether it is itself in the result, and
//     whether those images are faces of the result. Cases:
//       B1 two boxes side by side, same height: the tops merge;
//       B2 a boss on a block: the boss's bottom is inside, the block's top
//          gets a hole;
//       B3 a block cut by a through slot: its top splits in two;
//       B4 a block cut by a pocket: the pocket's floor is the tool's end;
//       B5 common of two overlapping boxes.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       face_history_probe.cpp -L<deps>/lib -lTKBO -lTKShHealing -lTKPrim
//       -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d -lTKG3d
//       -lTKMath -lTKernel
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

std::string describeFace(const TopoDS_Shape& shape) {
    const TopoDS_Face face = TopoDS::Face(shape);
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    const gp_Pnt c = props.CentreOfMass();
    const BRepAdaptor_Surface surface(face);
    char text[160];
    std::snprintf(text, sizeof text, "%s area %.2f centre (%.2f, %.2f, %.2f)",
                  surface.GetType() == GeomAbs_Plane ? "plane" : "curved", props.Mass(), c.X(), c.Y(), c.Z());
    return text;
}

TopoDS_Shape box(double x, double y, double z, double dx, double dy, double dz) {
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape();
}

void booleanCase(const char* name, BRepAlgoAPI_BooleanOperation& op, const TopoDS_Shape& a, const TopoDS_Shape& b) {
    NCollection_List<TopoDS_Shape> arguments;
    arguments.Append(a);
    NCollection_List<TopoDS_Shape> tools;
    tools.Append(b);
    op.SetArguments(arguments);
    op.SetTools(tools);
    op.SetRunParallel(false);
    op.Build();
    op.SimplifyResult();
    ShapeMap resultFaces;
    TopExp::MapShapes(op.Shape(), TopAbs_FACE, resultFaces);
    std::printf("%s: %s, %d faces in the result\n", name, op.IsDone() ? "done" : "FAILED", resultFaces.Extent());
    for (int i = 1; i <= resultFaces.Extent(); ++i) {
        std::printf("   result face %d: %s\n", i, describeFace(resultFaces(i)).c_str());
    }
    int operand = 0;
    for (const TopoDS_Shape* shape : {&a, &b}) {
        ++operand;
        ShapeMap faces;
        TopExp::MapShapes(*shape, TopAbs_FACE, faces);
        for (int i = 1; i <= faces.Extent(); ++i) {
            const TopoDS_Shape& face = faces(i);
            const bool deleted = op.IsDeleted(face);
            const NCollection_List<TopoDS_Shape>& images = op.Modified(face);
            std::printf("   operand %d face %d (%s): deleted %d, itself in result %d, modified into [", operand, i,
                        describeFace(face).c_str(), deleted ? 1 : 0, resultFaces.Contains(face) ? 1 : 0);
            for (const TopoDS_Shape& image : images) {
                std::printf(" %d", resultFaces.FindIndex(image));
            }
            std::printf(" ]\n");
        }
    }
}

} // namespace

int main() {
    std::printf("P12-STREF-001 face history probe (OCCT 8.0.1)\n\n");

    // A. A 100 x 60 rectangle with a radius-10 hole, extruded 20 along +Z from z = 5.
    const gp_Pln plane(gp_Ax3(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
    const TopoDS_Vertex v[4] = {BRepBuilderAPI_MakeVertex(gp_Pnt(0, 0, 5)),
                                BRepBuilderAPI_MakeVertex(gp_Pnt(100, 0, 5)),
                                BRepBuilderAPI_MakeVertex(gp_Pnt(100, 60, 5)),
                                BRepBuilderAPI_MakeVertex(gp_Pnt(0, 60, 5))};
    std::vector<TopoDS_Edge> edges;
    BRepBuilderAPI_MakeWire outer;
    for (int i = 0; i < 4; ++i) {
        edges.push_back(BRepBuilderAPI_MakeEdge(v[i], v[(i + 1) % 4]).Edge());
        outer.Add(edges.back());
    }
    const gp_Circ circle(gp_Ax2(gp_Pnt(30, 30, 5), gp_Dir(0, 0, -1)), 10);
    edges.push_back(BRepBuilderAPI_MakeEdge(circle).Edge());
    BRepBuilderAPI_MakeFace makeFace(plane, outer.Wire(), true);
    makeFace.Add(BRepBuilderAPI_MakeWire(edges.back()).Wire());
    const TopoDS_Face profile = makeFace.Face();
    ShapeMap profileEdges;
    TopExp::MapShapes(profile, TopAbs_EDGE, profileEdges);
    BRepPrimAPI_MakePrism prism(profile, gp_Vec(0, 0, 20));
    ShapeMap prismFaces;
    TopExp::MapShapes(prism.Shape(), TopAbs_FACE, prismFaces);
    std::printf("A. prism: %d faces; first shape %s (in result %d); last shape %s (in result %d)\n",
                prismFaces.Extent(), describeFace(prism.FirstShape()).c_str(),
                prismFaces.Contains(prism.FirstShape()) ? 1 : 0, describeFace(prism.LastShape()).c_str(),
                prismFaces.Contains(prism.LastShape()) ? 1 : 0);
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const NCollection_List<TopoDS_Shape>& generated = prism.Generated(edges[i]);
        std::printf("   wire edge %zu: a profile face edge %d; generates %d shape(s):", i,
                    profileEdges.Contains(edges[i]) ? 1 : 0, generated.Extent());
        for (const TopoDS_Shape& s : generated) {
            std::printf(" [%s, result face %d]", s.ShapeType() == TopAbs_FACE ? describeFace(s).c_str() : "not a face",
                        prismFaces.FindIndex(s));
        }
        std::printf("\n");
    }
    std::printf("\n");

    {
        BRepAlgoAPI_Fuse fuse;
        booleanCase("B1 fuse of side-by-side boxes", fuse, box(0, 0, 0, 100, 60, 20), box(100, 0, 0, 50, 60, 20));
    }
    {
        BRepAlgoAPI_Fuse fuse;
        booleanCase("B2 fuse of a boss on a block", fuse, box(0, 0, 0, 100, 60, 20), box(20, 20, 20, 20, 20, 10));
    }
    {
        BRepAlgoAPI_Cut cut;
        booleanCase("B3 cut of a through slot", cut, box(0, 0, 0, 100, 60, 20), box(40, -1, 10, 20, 62, 11));
    }
    {
        BRepAlgoAPI_Cut cut;
        booleanCase("B4 cut of a pocket", cut, box(0, 0, 0, 100, 60, 20), box(20, 20, 12, 30, 20, 8));
    }
    {
        BRepAlgoAPI_Common common;
        booleanCase("B5 common of overlapping boxes", common, box(0, 0, 0, 100, 60, 20), box(50, 30, 10, 100, 60, 20));
    }
    return 0;
}
