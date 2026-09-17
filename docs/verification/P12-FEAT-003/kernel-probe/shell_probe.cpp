// Kernel probe (evidence, not part of the build): what OCCT 8.0.1's
// BRepOffsetAPI_MakeThickSolid::MakeThickSolidByJoin gives for the shells
// P12-FEAT-003 builds, against analytic volumes, and the history it reports.
//
//  A. A 100 x 60 x 40 box, top removed, 5 mm inward, both join types.
//  B. The same box, 5 mm outward, both join types.
//  C. A cylinder r 20 x 50, top removed, 3 mm inward.
//  D. The box with no face removed, 5 mm inward (a closed hollow).
//  E. The box, top removed, walls too thick (35 mm inward: more than half
//     of 60).
//  F. The box with a through hole r 10, top removed, 5 mm inward.
//  H. An L-shaped prism (one reflex edge), top removed, 5 mm inward (arc
//     and intersection) and outward (arc).
//  I. The box with top and bottom removed (a tube).
//  J. The cylinder with its curved side removed.
//  K. What the kernel reports for E: its error code and history.
//  G. History: for each input face of A, IsDeleted(), Modified() and
//     Generated(), and whether the face itself is in the result.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       shell_probe.cpp -L<deps>/lib -lTKOffset -lTKFillet -lTKBO
//       -lTKShHealing -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep
//       -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_List.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <gp_Ax2.hxx>

#include <NCollection_IndexedMap.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using ShapeList = NCollection_List<TopoDS_Shape>;

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

std::string describe(const TopoDS_Face& face) {
    BRepAdaptor_Surface surface(face);
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    const gp_Pnt c = props.CentreOfMass();
    const char* kind = surface.GetType() == GeomAbs_Plane      ? "plane"
                       : surface.GetType() == GeomAbs_Cylinder ? "cylinder"
                                                               : "other";
    char text[160];
    std::snprintf(text, sizeof text, "%s area %.3f centre (%.3f, %.3f, %.3f)", kind, props.Mass(), c.X(), c.Y(),
                  c.Z());
    return text;
}

/// The face of @p shape whose centroid is nearest @p point.
TopoDS_Face faceNear(const TopoDS_Shape& shape, const gp_Pnt& point) {
    TopoDS_Face best;
    double distance = 1e300;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(it.Current(), props);
        const double d = props.CentreOfMass().Distance(point);
        if (d < distance) {
            distance = d;
            best = TopoDS::Face(it.Current());
        }
    }
    return best;
}

struct Outcome {
    bool done = false;
    bool valid = false;
    double volume = 0.0;
    int faces = 0;
    int solids = 0;
    double seconds = 0.0;
    std::string error;
};

Outcome shell(BRepOffsetAPI_MakeThickSolid& maker, const TopoDS_Shape& shape, const ShapeList& removed,
              double offset, GeomAbs_JoinType join) {
    Outcome out;
    const auto start = std::chrono::steady_clock::now();
    try {
        maker.MakeThickSolidByJoin(shape, removed, offset, 1e-7, BRepOffset_Skin, false, false, join);
        out.done = maker.IsDone();
        if (out.done) {
            const TopoDS_Shape& result = maker.Shape();
            out.valid = BRepCheck_Analyzer(result).IsValid();
            out.volume = volumeOf(result);
            ShapeMap faces;
            TopExp::MapShapes(result, TopAbs_FACE, faces);
            out.faces = faces.Extent();
            ShapeMap solids;
            TopExp::MapShapes(result, TopAbs_SOLID, solids);
            out.solids = solids.Extent();
        } else {
            out.error = "not done";
        }
    } catch (const Standard_Failure& failure) {
        out.error = std::string("exception: ") + failure.what();
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

void report(const char* name, const Outcome& out, double expected) {
    if (!out.done) {
        std::printf("%s: FAILED (%s) in %.3f s\n", name, out.error.c_str(), out.seconds);
        return;
    }
    std::printf("%s: valid %d, solids %d, faces %d, volume %.9f, expected %.9f, rel %.2e, %.3f s\n", name,
                out.valid ? 1 : 0, out.solids, out.faces, out.volume, expected,
                std::abs(out.volume - expected) / expected, out.seconds);
}

} // namespace

int main() {
    const double pi = M_PI;
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 40.0).Shape();
    const TopoDS_Face top = faceNear(box, gp_Pnt(50, 30, 40));
    ShapeList topOnly;
    topOnly.Append(top);

    std::printf("OCCT shell probe\n\n");
    for (const GeomAbs_JoinType join : {GeomAbs_Arc, GeomAbs_Intersection}) {
        const char* joinName = join == GeomAbs_Arc ? "arc" : "intersection";
        BRepOffsetAPI_MakeThickSolid inward;
        char name[80];
        std::snprintf(name, sizeof name, "A. box, top removed, 5 inward, %s", joinName);
        report(name, shell(inward, box, topOnly, -5.0, join), 240000.0 - 90.0 * 50.0 * 35.0);
        BRepOffsetAPI_MakeThickSolid outward;
        std::snprintf(name, sizeof name, "B. box, top removed, 5 outward, %s", joinName);
        // Intersection: a 110 x 70 x 45 box; arc: the box dilated by a ball
        // (faces, quarter-round edges, sphere-octant corners) below z = 40.
        const double outer = join == GeomAbs_Arc ? 94000.0 + 3000.0 * pi + 250.0 * pi / 3.0
                                                 : 110.0 * 70.0 * 45.0 - 240000.0;
        report(name, shell(outward, box, topOnly, 5.0, join), outer);
    }

    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 20.0, 50.0).Shape();
    ShapeList cylinderTop;
    cylinderTop.Append(faceNear(cylinder, gp_Pnt(0, 0, 50)));
    {
        BRepOffsetAPI_MakeThickSolid cup;
        report("C. cylinder, top removed, 3 inward, arc", shell(cup, cylinder, cylinderTop, -3.0, GeomAbs_Arc),
               pi * 400.0 * 50.0 - pi * 17.0 * 17.0 * 47.0);
    }
    {
        BRepOffsetAPI_MakeThickSolid closed;
        report("D. box, nothing removed, 5 inward, arc", shell(closed, box, ShapeList{}, -5.0, GeomAbs_Arc),
               240000.0 - 90.0 * 50.0 * 30.0);
    }
    {
        BRepOffsetAPI_MakeThickSolid thick;
        report("E. box, top removed, 35 inward, arc", shell(thick, box, topOnly, -35.0, GeomAbs_Arc), 1.0);
        BRepOffsetAPI_MakeThickSolid thirty;
        report("E. box, top removed, 30 inward (half of 60), arc", shell(thirty, box, topOnly, -30.0, GeomAbs_Arc),
               1.0);
        BRepOffsetAPI_MakeThickSolid almost;
        report("E. box, top removed, 29 inward, arc", shell(almost, box, topOnly, -29.0, GeomAbs_Arc),
               240000.0 - 42.0 * 2.0 * 11.0);
    }
    {
        const TopoDS_Shape hole =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50, 30, -1), gp_Dir(0, 0, 1)), 10.0, 42.0).Shape();
        const TopoDS_Shape drilled = BRepAlgoAPI_Cut(box, hole).Shape();
        ShapeList drilledTop;
        drilledTop.Append(faceNear(drilled, gp_Pnt(50, 30, 40)));
        BRepOffsetAPI_MakeThickSolid withHole;
        report("F. drilled box, top removed, 5 inward, arc", shell(withHole, drilled, drilledTop, -5.0, GeomAbs_Arc),
               82500.0 + 3875.0 * pi);
    }

    {
        // An L-shaped prism: arms 100 x 30 and 40 x 80, 50 high; its one
        // reflex edge is vertical at (40, 30). Inward, the cavity's corner
        // there is a quarter round of radius t about the edge; outward, the
        // outer faces are the prism dilated by a ball of radius t.
        BRepBuilderAPI_MakePolygon polygon;
        const std::pair<double, double> corners[] = {{0.0, 0.0},   {100.0, 0.0}, {100.0, 30.0},
                                                     {40.0, 30.0}, {40.0, 80.0}, {0.0, 80.0}};
        for (const auto& [x, y] : corners) {
            polygon.Add(gp_Pnt(x, y, 0.0));
        }
        polygon.Close();
        const TopoDS_Face base = BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
        const TopoDS_Shape ell = BRepPrimAPI_MakePrism(base, gp_Vec(0, 0, 50)).Shape();
        ShapeList ellTop;
        ellTop.Append(faceNear(ell, gp_Pnt(38, 31, 50)));
        std::printf("   L top face picked: %s\n", describe(TopoDS::Face(ellTop.First())).c_str());
        const double t = 5.0;
        const double eroded = 90.0 * 20.0 + 30.0 * 70.0 - 30.0 * 20.0 + t * t * (1.0 - pi / 4.0);
        BRepOffsetAPI_MakeThickSolid inward;
        report("H. L prism, top removed, 5 inward, arc", shell(inward, ell, ellTop, -t, GeomAbs_Arc),
               5000.0 * 50.0 - eroded * 45.0);
        BRepOffsetAPI_MakeThickSolid sharp;
        report("H. L prism, top removed, 5 inward, intersection (sharp cavity)",
               shell(sharp, ell, ellTop, -t, GeomAbs_Intersection),
               5000.0 * 50.0 - (90.0 * 20.0 + 30.0 * 70.0 - 30.0 * 20.0) * 45.0);
        // Dilated section D(r) = A + P r + (5 pi / 4 - 1) r^2 (five convex
        // corners, one reflex); the bottom slab integrates D(sqrt(t^2 - s^2)).
        const double sides = (5000.0 + 360.0 * t + (5.0 * pi / 4.0 - 1.0) * t * t) * 50.0;
        const double bottom = 5000.0 * t + 360.0 * pi * t * t / 4.0 + (5.0 * pi / 4.0 - 1.0) * 2.0 * t * t * t / 3.0;
        BRepOffsetAPI_MakeThickSolid outward;
        report("H. L prism, top removed, 5 outward, arc", shell(outward, ell, ellTop, t, GeomAbs_Arc),
               sides + bottom - 250000.0);
    }
    {
        ShapeList both = topOnly;
        both.Append(faceNear(box, gp_Pnt(50, 30, 0)));
        BRepOffsetAPI_MakeThickSolid tube;
        report("I. box, top and bottom removed, 5 inward, arc", shell(tube, box, both, -5.0, GeomAbs_Arc),
               240000.0 - 90.0 * 50.0 * 40.0);
        ShapeList lateral;
        for (TopExp_Explorer it(cylinder, TopAbs_FACE); it.More(); it.Next()) {
            if (BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType() == GeomAbs_Cylinder) {
                lateral.Append(it.Current());
            }
        }
        BRepOffsetAPI_MakeThickSolid discs;
        report("J. cylinder, curved side removed, 3 inward, arc", shell(discs, cylinder, lateral, -3.0, GeomAbs_Arc),
               2.0 * pi * 400.0 * 3.0);
    }
    {
        std::printf("\nK. history and error of E (35 inward)\n");
        BRepOffsetAPI_MakeThickSolid thick;
        shell(thick, box, topOnly, -35.0, GeomAbs_Arc);
        std::printf("  done %d, error %d, result is the input %d\n", thick.IsDone() ? 1 : 0,
                    static_cast<int>(thick.MakeOffset().Error()), thick.Shape().IsSame(box) ? 1 : 0);
        ShapeMap thickFaces;
        TopExp::MapShapes(thick.Shape(), TopAbs_FACE, thickFaces);
        for (TopExp_Explorer it(box, TopAbs_FACE); it.More(); it.Next()) {
            std::printf("  face (%s): deleted %d, in result %d, modified %d, generated %d\n",
                        describe(TopoDS::Face(it.Current())).c_str(), thick.IsDeleted(it.Current()) ? 1 : 0,
                        thickFaces.Contains(it.Current()) ? 1 : 0, thick.Modified(it.Current()).Extent(),
                        thick.Generated(it.Current()).Extent());
        }
        BRepOffsetAPI_MakeThickSolid thirty;
        shell(thirty, box, topOnly, -30.0, GeomAbs_Arc);
        std::printf("  30 inward: done %d, error %d\n", thirty.IsDone() ? 1 : 0,
                    static_cast<int>(thirty.MakeOffset().Error()));
    }

    {
        std::printf("\nL. history of B (outward, intersection): the removed top\n");
        BRepOffsetAPI_MakeThickSolid outward;
        shell(outward, box, topOnly, 5.0, GeomAbs_Intersection);
        ShapeMap outwardFaces;
        TopExp::MapShapes(outward.Shape(), TopAbs_FACE, outwardFaces);
        std::printf("  top: deleted %d, in result %d\n", outward.IsDeleted(top) ? 1 : 0,
                    outwardFaces.Contains(top) ? 1 : 0);
        for (const TopoDS_Shape& image : outward.Modified(top)) {
            std::printf("    modified -> %s, in result %d\n", describe(TopoDS::Face(image)).c_str(),
                        outwardFaces.Contains(image) ? 1 : 0);
        }
        for (const TopoDS_Shape& image : outward.Generated(top)) {
            std::printf("    generated -> %s, in result %d\n", describe(TopoDS::Face(image)).c_str(),
                        outwardFaces.Contains(image) ? 1 : 0);
        }
        for (int i = 1; i <= outwardFaces.Extent(); ++i) {
            std::printf("    result face %d: %s\n", i, describe(TopoDS::Face(outwardFaces(i))).c_str());
        }
    }

    std::printf("\nG. history of A (inward, arc)\n");
    BRepOffsetAPI_MakeThickSolid maker;
    shell(maker, box, topOnly, -5.0, GeomAbs_Arc);
    ShapeMap resultFaces;
    TopExp::MapShapes(maker.Shape(), TopAbs_FACE, resultFaces);
    int index = 0;
    for (TopExp_Explorer it(box, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        ++index;
        std::printf("  input face %d (%s): deleted %d, itself in result %d\n", index, describe(face).c_str(),
                    maker.IsDeleted(face) ? 1 : 0, resultFaces.Contains(face) ? 1 : 0);
        for (const TopoDS_Shape& image : maker.Modified(face)) {
            std::printf("    modified -> %s, in result %d\n",
                        image.ShapeType() == TopAbs_FACE ? describe(TopoDS::Face(image)).c_str() : "not a face",
                        resultFaces.Contains(image) ? 1 : 0);
        }
        for (const TopoDS_Shape& image : maker.Generated(face)) {
            std::printf("    generated -> %s, in result %d\n",
                        image.ShapeType() == TopAbs_FACE ? describe(TopoDS::Face(image)).c_str() : "not a face",
                        resultFaces.Contains(image) ? 1 : 0);
        }
    }
    std::printf("  result faces:\n");
    for (int i = 1; i <= resultFaces.Extent(); ++i) {
        std::printf("    %d: %s\n", i, describe(TopoDS::Face(resultFaces(i))).c_str());
    }
    return 0;
}
