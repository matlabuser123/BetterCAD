// Kernel probe (evidence, not part of the build): the history OCCT 8.0.1
// reports for the operations P12-SKETCH-003 names faces through, beyond the
// prisms and booleans of P12-STREF-001.
//
//  A. BRepPrimAPI_MakeRevol, a quarter turn and a full turn:
//     FirstShape()/LastShape() and Generated(edge) of the profile's edges,
//     and the sweep's own Revol().Shape(edge) (the face each edge sweeps,
//     as the sweep built it).
//  B. BRepOffsetAPI_MakePipeShell along a line, an arc and a line (open) and
//     along a circle (closed): FirstShape()/LastShape() and Generated(edge)
//     of the profile's edges, in the order the faces follow the path.
//  C. BRepOffsetAPI_ThruSections, ruled, two squares:
//     FirstShape()/LastShape() and GeneratedFace(edge).
//  D. A hole cutter (a counterbored section revolved a full turn):
//     Generated(edge) of the section's edges, and the cutter's faces through
//     a boolean cut.
//  E. BRepFilletAPI_MakeChamfer and F. BRepFilletAPI_MakeFillet on one edge
//     of a box: Generated(edge), Modified() and IsDeleted() of its faces.
//  G. BRepBuilderAPI_Transform (copy), a translation and a reflection:
//     Modified() of each face.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       generation_history_probe.cpp -L<deps>/lib -lTKFillet -lTKOffset
//       -lTKBO -lTKShHealing -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep
//       -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_TransitionMode.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepSweep_Revol.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using ShapeList = NCollection_List<TopoDS_Shape>;

std::string describe(const TopoDS_Shape& shape) {
    if (shape.IsNull()) {
        return "null";
    }
    if (shape.ShapeType() != TopAbs_FACE) {
        return std::string("not a face (type ") + std::to_string(static_cast<int>(shape.ShapeType())) + ")";
    }
    const TopoDS_Face face = TopoDS::Face(shape);
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    const gp_Pnt c = props.CentreOfMass();
    const BRepAdaptor_Surface surface(face);
    static const char* const kinds[] = {"plane", "cylinder", "cone", "sphere", "torus", "bezier", "bspline",
                                        "revolution", "extrusion", "offset", "other"};
    const int type = static_cast<int>(surface.GetType());
    char text[200];
    std::snprintf(text, sizeof text, "%s area %.3f centre (%.3f, %.3f, %.3f)", type <= 10 ? kinds[type] : "?",
                  props.Mass(), c.X(), c.Y(), c.Z());
    return text;
}

void printList(const char* label, const ShapeList& shapes, const ShapeMap& result) {
    std::printf("      %s: %d shape(s)", label, shapes.Extent());
    for (const TopoDS_Shape& s : shapes) {
        std::printf("\n        [%s, result face %d]", describe(s).c_str(), result.FindIndex(s));
    }
    std::printf("\n");
}

TopoDS_Wire rectangle(double x0, double y0, double x1, double y1, double z, std::vector<TopoDS_Edge>& edges) {
    BRepBuilderAPI_MakePolygon polygon(gp_Pnt(x0, y0, z), gp_Pnt(x1, y0, z), gp_Pnt(x1, y1, z), gp_Pnt(x0, y1, z),
                                       true);
    const TopoDS_Wire wire = polygon.Wire();
    for (TopExp_Explorer it(wire, TopAbs_EDGE); it.More(); it.Next()) {
        edges.push_back(TopoDS::Edge(it.Current()));
    }
    return wire;
}

TopoDS_Face planeFace(const TopoDS_Wire& wire, const gp_Ax3& frame) {
    return BRepBuilderAPI_MakeFace(gp_Pln(frame), wire, true).Face();
}

void faceList(const char* label, const TopoDS_Shape& shape, ShapeMap& faces) {
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    std::printf("   %s: %d faces\n", label, faces.Extent());
}

void historyOfFaces(BRepBuilderAPI_MakeShape& op, const TopoDS_Shape& input, const ShapeMap& result) {
    ShapeMap faces;
    TopExp::MapShapes(input, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        const ShapeList& images = op.Modified(faces(i));
        std::printf("      input face %d (%s): deleted %d, itself in result %d, modified into [", i,
                    describe(faces(i)).c_str(), op.IsDeleted(faces(i)) ? 1 : 0, result.Contains(faces(i)) ? 1 : 0);
        for (const TopoDS_Shape& image : images) {
            std::printf(" %d", result.FindIndex(image));
        }
        std::printf(" ]\n");
    }
}

} // namespace

int main() {
    std::printf("P12-SKETCH-003 generation history probe (OCCT 8.0.1)\n\n");
    const gp_Ax3 xy(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));

    // A. A 10 x 20 rectangle at x 40..50, y 0..20, revolved about the Y axis.
    for (const double angle : {M_PI / 2, 2 * M_PI}) {
        std::vector<TopoDS_Edge> edges;
        const TopoDS_Face profile = planeFace(rectangle(40, 0, 50, 20, 0, edges), xy);
        const gp_Ax1 axis(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
        BRepPrimAPI_MakeRevol revol = angle < 2 * M_PI ? BRepPrimAPI_MakeRevol(profile, axis, angle)
                                                       : BRepPrimAPI_MakeRevol(profile, axis);
        ShapeMap result;
        std::printf("A. revolution by %.0f deg\n", angle * 180 / M_PI);
        faceList("result", revol.Shape(), result);
        std::printf("      first shape: %s (result face %d)\n", describe(revol.FirstShape()).c_str(),
                    result.FindIndex(revol.FirstShape()));
        std::printf("      last shape: %s (result face %d)\n", describe(revol.LastShape()).c_str(),
                    result.FindIndex(revol.LastShape()));
        for (std::size_t i = 0; i < edges.size(); ++i) {
            printList(("edge " + std::to_string(i) + " generated").c_str(), revol.Generated(edges[i]), result);
            // Shape() is not const; the sweep is not changed by the query.
            const TopoDS_Shape swept = const_cast<BRepSweep_Revol&>(revol.Revol()).Shape(edges[i]);
            std::printf("      edge %zu Revol().Shape: %s (result face %d)\n", i, describe(swept).c_str(),
                        result.FindIndex(swept));
        }
    }
    std::printf("\n");

    // B. A 6 x 4 rectangle in the XZ... plane at the origin, swept up Z 40,
    //    round a quarter turn of radius 60 towards +X, then 30 along +X.
    for (const bool closed : {false, true}) {
        BRepBuilderAPI_MakeWire spine;
        if (!closed) {
            const gp_Pnt a(0, 0, 0);
            const gp_Pnt b(0, 0, 40);
            const gp_Pnt c(60, 0, 100);
            const gp_Pnt d(90, 0, 100);
            spine.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
            spine.Add(BRepBuilderAPI_MakeEdge(
                          GC_MakeArcOfCircle(b, gp_Pnt(60 - 60 * std::cos(M_PI / 4), 0, 40 + 60 * std::sin(M_PI / 4)), c)
                              .Value())
                          .Edge());
            spine.Add(BRepBuilderAPI_MakeEdge(c, d).Edge());
        } else {
            // A circle of radius 60 in the XZ plane through the origin, starting up +Z.
            spine.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(60, 0, 0), gp_Dir(0, 1, 0), gp_Dir(-1, 0, 0)), 60))
                          .Edge());
        }
        std::vector<TopoDS_Edge> edges;
        const TopoDS_Wire loop = rectangle(-3, -2, 3, 2, 0, edges);
        BRepOffsetAPI_MakePipeShell pipe(spine.Wire());
        pipe.SetMode(gp_Dir(0, -1, 0));
        pipe.SetTransitionMode(BRepBuilderAPI_RightCorner);
        pipe.Add(loop, false, false);
        pipe.Build();
        pipe.MakeSolid();
        ShapeMap result;
        std::printf("B. pipe shell along a %s path: %s\n", closed ? "closed (circle)" : "line, arc, line",
                    pipe.IsDone() ? "done" : "FAILED");
        if (!pipe.IsDone()) {
            continue;
        }
        faceList("result", pipe.Shape(), result);
        std::printf("      first shape: %s (result face %d)\n", describe(pipe.FirstShape()).c_str(),
                    result.FindIndex(pipe.FirstShape()));
        std::printf("      last shape: %s (result face %d)\n", describe(pipe.LastShape()).c_str(),
                    result.FindIndex(pipe.LastShape()));
        for (std::size_t i = 0; i < edges.size(); ++i) {
            printList(("profile edge " + std::to_string(i) + " generated").c_str(), pipe.Generated(edges[i]),
                      result);
        }
    }
    std::printf("\n");

    // C. A ruled loft between a 20 and a 10 square, 30 apart.
    {
        std::vector<TopoDS_Edge> first;
        std::vector<TopoDS_Edge> second;
        BRepOffsetAPI_ThruSections loft(true, true);
        loft.CheckCompatibility(false);
        loft.AddWire(rectangle(-10, -10, 10, 10, 0, first));
        loft.AddWire(rectangle(-5, -5, 5, 5, 30, second));
        loft.Build();
        ShapeMap result;
        std::printf("C. ruled loft: %s\n", loft.IsDone() ? "done" : "FAILED");
        faceList("result", loft.Shape(), result);
        std::printf("      first shape: %s (result face %d)\n", describe(loft.FirstShape()).c_str(),
                    result.FindIndex(loft.FirstShape()));
        std::printf("      last shape: %s (result face %d)\n", describe(loft.LastShape()).c_str(),
                    result.FindIndex(loft.LastShape()));
        for (std::size_t i = 0; i < first.size(); ++i) {
            const TopoDS_Shape face = loft.GeneratedFace(first[i]);
            std::printf("      first-section edge %zu: generated face %s (result face %d)\n", i,
                        describe(face).c_str(), result.FindIndex(face));
        }
    }
    std::printf("\n");

    // D. A counterbored hole cutter: the section (radius, depth) in the XZ
    //    plane, revolved about Z; then cut from a 40 x 40 x 20 block from its top.
    {
        const double points[][2] = {{0, 21}, {8, 21}, {8, 15}, {4, 15}, {4, 5}, {0, 5}};
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& p : points) {
            polygon.Add(gp_Pnt(20 + p[0], 20, p[1]));
        }
        polygon.Close();
        const TopoDS_Wire wire = polygon.Wire();
        std::vector<TopoDS_Edge> edges;
        for (TopExp_Explorer it(wire, TopAbs_EDGE); it.More(); it.Next()) {
            edges.push_back(TopoDS::Edge(it.Current()));
        }
        const TopoDS_Face section =
            planeFace(wire, gp_Ax3(gp_Pnt(20, 20, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0)));
        BRepPrimAPI_MakeRevol revol(section, gp_Ax1(gp_Pnt(20, 20, 0), gp_Dir(0, 0, 1)));
        ShapeMap cutterFaces;
        std::printf("D. counterbored cutter\n");
        faceList("cutter", revol.Shape(), cutterFaces);
        for (std::size_t i = 0; i < edges.size(); ++i) {
            printList(("section edge " + std::to_string(i) + " generated").c_str(), revol.Generated(edges[i]),
                      cutterFaces);
            const TopoDS_Shape swept = const_cast<BRepSweep_Revol&>(revol.Revol()).Shape(edges[i]);
            std::printf("      section edge %zu Revol().Shape: %s (cutter face %d)\n", i, describe(swept).c_str(),
                        cutterFaces.FindIndex(swept));
        }
        const TopoDS_Shape block = BRepPrimAPI_MakeBox(40, 40, 20).Shape();
        BRepAlgoAPI_Cut cut(block, revol.Shape());
        cut.SimplifyResult();
        ShapeMap result;
        faceList("drilled block", cut.Shape(), result);
        historyOfFaces(cut, revol.Shape(), result);
    }
    std::printf("\n");

    // E, F. A 40 x 30 x 20 box; the edge along X at y = 0, z = 20.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40, 30, 20).Shape();
    TopoDS_Edge topFront;
    NCollection_IndexedDataMap<TopoDS_Shape, ShapeList, TopTools_ShapeMapHasher> edgeFaces;
    TopExp::MapShapesAndAncestors(box, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    for (int i = 1; i <= edgeFaces.Extent(); ++i) {
        GProp_GProps props;
        BRepGProp::LinearProperties(edgeFaces.FindKey(i), props);
        const gp_Pnt c = props.CentreOfMass();
        if (std::abs(c.Y()) < 1e-9 && std::abs(c.Z() - 20) < 1e-9) {
            topFront = TopoDS::Edge(edgeFaces.FindKey(i));
        }
    }
    {
        BRepFilletAPI_MakeChamfer chamfer(box);
        const TopoDS_Face& face = TopoDS::Face(edgeFaces.FindFromKey(topFront).First());
        chamfer.Add(3.0, 3.0, topFront, face);
        chamfer.Build();
        ShapeMap result;
        std::printf("E. chamfer: %s\n", chamfer.IsDone() ? "done" : "FAILED");
        faceList("result", chamfer.Shape(), result);
        printList("edge generated", chamfer.Generated(topFront), result);
        historyOfFaces(chamfer, box, result);
    }
    {
        BRepFilletAPI_MakeFillet fillet(box);
        fillet.Add(3.0, topFront);
        fillet.Build();
        ShapeMap result;
        std::printf("F. fillet: %s\n", fillet.IsDone() ? "done" : "FAILED");
        faceList("result", fillet.Shape(), result);
        printList("edge generated", fillet.Generated(topFront), result);
        historyOfFaces(fillet, box, result);
    }
    std::printf("\n");

    for (const bool reflect : {false, true}) {
        gp_Trsf trsf;
        if (reflect) {
            trsf.SetMirror(gp_Ax2(gp_Pnt(60, 0, 0), gp_Dir(1, 0, 0)));
        } else {
            trsf.SetTranslation(gp_Vec(100, 0, 0));
        }
        BRepBuilderAPI_Transform transform(box, trsf, /*copy=*/true);
        ShapeMap result;
        std::printf("G. transformed copy (%s)\n", reflect ? "reflection" : "translation");
        faceList("result", transform.Shape(), result);
        historyOfFaces(transform, box, result);
    }
    return 0;
}
