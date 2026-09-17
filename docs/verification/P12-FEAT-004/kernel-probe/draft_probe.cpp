// Kernel probe (evidence, not part of the build): what OCCT 8.0.1's
// BRepOffsetAPI_DraftAngle gives for the drafts P12-FEAT-004 builds, against
// analytic volumes, and what it reports.
//
// Every drafted face turns by the angle about its line on the neutral plane,
// so at height z above that plane (along the pull direction) a vertical face
// has moved z tan(a) inward (positive angles remove material above the
// plane). A section whose outline moves in by d(z) has an area that is a
// polynomial in z, integrated exactly below.
//
//  A. A 100 x 60 x 40 box, its four sides drafted 5 deg, neutral plane z = 0.
//  B. The same about z = 20 (the bottom grows, the top shrinks).
//  C. The same at -5 deg (the walls lean out going up).
//  D. One side (x = 100) drafted 10 deg.
//  E. A cylinder r 20 x 50, its side drafted 3 deg about z = 0: a frustum.
//  F. The box with an r 10 through hole: the hole drafted 2 deg (it widens
//     going up).
//  G. The box with its vertical edges rounded r 10: the four sides drafted
//     5 deg; the rounds follow (tangent faces), their radius r - d(z).
//  H. Angles too large for the box: the sides drafted 45 deg and 60 deg
//     (the top would vanish or turn inside out), and one face 89 deg.
//  I. A face that cannot be drafted: the box's top, whose plane is parallel
//     to the neutral plane; and the pull direction along the face.
//  J. Zero angles (a box, a cylinder).
//  H7. Angles past the one that makes the box's top degenerate.
//  K. History and topology of A: Modified(), Generated() and
//     ModifiedShape() of each face; face, edge and vertex counts before and
//     after, for every case.
//  K2. A pocket's walls drafted about the top it opens in.
//  L. An L-shaped prism's six sides (one concave edge).
//  M. A box with rounded top edges: its sides are tangent to the rounds,
//     which are tangent to the top.
// Every result is also run through BRepAlgoAPI_Check's self-interference
// test, which BRepCheck_Analyzer does not do.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       draft_probe.cpp -L<deps>/lib -lTKOffset -lTKFillet -lTKBO
//       -lTKShHealing -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep
//       -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Draft_ErrorStatus.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
constexpr double pi = M_PI;
constexpr double none = std::numeric_limits<double>::quiet_NaN();

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    ShapeMap map;
    TopExp::MapShapes(shape, type, map);
    return map.Extent();
}

/// Integral over z in [z0, z1] of a polynomial c0 + c1 z + c2 z^2.
double integrate(double c0, double c1, double c2, double z0, double z1) {
    const auto f = [&](double z) { return c0 * z + c1 * z * z / 2.0 + c2 * z * z * z / 3.0; };
    return f(z1) - f(z0);
}

/// Volume of a prism along Z over a w x d rectangle with corner radius r
/// (0 for sharp corners) whose outline moves in by s(z) = (z - z0) t, from
/// zb to zt.
double shrinkingPrism(double w, double d, double r, double t, double zNeutral, double zb, double zt) {
    // A(z) = (w - 2s)(d - 2s) - k (r - s)^2, with k = 4 - pi for rounded
    // corners (radius r - s) and k = 0 for sharp ones.
    // In s: A = w d - k r^2 + s (-2w - 2d + 2 k r) + s^2 (4 - k).
    const double k = r > 0.0 ? 4.0 - pi : 0.0;
    const double a0 = w * d - k * r * r;
    const double a1 = -2.0 * w - 2.0 * d + 2.0 * k * r;
    const double a2 = 4.0 - k;
    // s = t z - t zNeutral: substitute.
    const double b = -t * zNeutral;
    const double c0 = a0 + a1 * b + a2 * b * b;
    const double c1 = a1 * t + 2.0 * a2 * b * t;
    const double c2 = a2 * t * t;
    return integrate(c0, c1, c2, zb, zt);
}

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

std::vector<TopoDS_Face> verticalFaces(const TopoDS_Shape& shape) {
    std::vector<TopoDS_Face> faces;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane) {
            if (std::abs(surface.Plane().Axis().Direction().Z()) < 1e-12) {
                faces.push_back(face);
            }
        } else if (surface.GetType() == GeomAbs_Cylinder) {
            faces.push_back(face);
        }
    }
    return faces;
}

const char* statusText(Draft_ErrorStatus status) {
    switch (status) {
    case Draft_NoError:
        return "no error";
    case Draft_FaceRecomputation:
        return "face recomputation";
    case Draft_EdgeRecomputation:
        return "edge recomputation";
    case Draft_VertexRecomputation:
        return "vertex recomputation";
    }
    return "?";
}

struct Run {
    const char* name;
    TopoDS_Shape shape;
    std::vector<TopoDS_Face> faces;
    double angleDeg;
    double neutralZ;
    gp_Dir pull;
    double expected;
};

void run(const Run& r, bool history = false) {
    const auto start = std::chrono::steady_clock::now();
    std::printf("%s: ", r.name);
    try {
        BRepOffsetAPI_DraftAngle draft(r.shape);
        const gp_Pln neutral(gp_Pnt(0, 0, r.neutralZ), gp_Dir(0, 0, 1));
        for (const TopoDS_Face& face : r.faces) {
            draft.Add(face, r.pull, r.angleDeg * pi / 180.0, neutral);
            if (!draft.AddDone()) {
                std::printf("ADD FAILED (%s)\n", statusText(draft.Status()));
                return;
            }
        }
        draft.Build();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!draft.IsDone()) {
            std::printf("BUILD FAILED (%s), %.3f s\n", statusText(draft.Status()), seconds);
            return;
        }
        const TopoDS_Shape& result = draft.Shape();
        const bool valid = BRepCheck_Analyzer(result).IsValid();
        const bool selfIntersects = !BRepAlgoAPI_Check(result, false, true).IsValid();
        const double volume = volumeOf(result);
        std::printf("valid %d self-intersects %d solids %d faces %d/%d edges %d/%d vertices %d/%d volume %.9f", valid ? 1 : 0,
                    selfIntersects ? 1 : 0, count(result, TopAbs_SOLID), count(result, TopAbs_FACE), count(r.shape, TopAbs_FACE),
                    count(result, TopAbs_EDGE), count(r.shape, TopAbs_EDGE), count(result, TopAbs_VERTEX),
                    count(r.shape, TopAbs_VERTEX), volume);
        if (!std::isnan(r.expected)) {
            std::printf(" expected %.9f rel %.1e", r.expected, std::abs(volume - r.expected) / r.expected);
        }
        std::printf(", %.3f s\n", seconds);
        if (history) {
            ShapeMap resultFaces;
            TopExp::MapShapes(result, TopAbs_FACE, resultFaces);
            for (TopExp_Explorer it(r.shape, TopAbs_FACE); it.More(); it.Next()) {
                GProp_GProps props;
                BRepGProp::SurfaceProperties(it.Current(), props);
                const gp_Pnt c = props.CentreOfMass();
                std::printf("  face at (%.1f, %.1f, %.1f): deleted %d, in result %d, modified %d", c.X(), c.Y(),
                            c.Z(), draft.IsDeleted(it.Current()) ? 1 : 0,
                            resultFaces.Contains(it.Current()) ? 1 : 0, draft.Modified(it.Current()).Extent());
                for (const TopoDS_Shape& image : draft.Modified(it.Current())) {
                    std::printf(" (image in result %d)", resultFaces.Contains(image) ? 1 : 0);
                }
                std::printf(", generated %d", draft.Generated(it.Current()).Extent());
                try {
                    const TopoDS_Shape shape = draft.ModifiedShape(it.Current());
                    GProp_GProps imageProps;
                    BRepGProp::SurfaceProperties(shape, imageProps);
                    const gp_Pnt ic = imageProps.CentreOfMass();
                    std::printf(", ModifiedShape: %s, in result %d, same as input %d, centre (%.3f, %.3f, %.3f)",
                                shape.IsNull() ? "null" : "a shape", resultFaces.Contains(shape) ? 1 : 0,
                                shape.IsSame(it.Current()) ? 1 : 0, ic.X(), ic.Y(), ic.Z());
                } catch (const Standard_Failure& failure) {
                    std::printf(", ModifiedShape threw %s", failure.ExceptionType());
                }
                std::printf("\n");
            }
        }
    } catch (const Standard_Failure& failure) {
        std::printf("EXCEPTION %s: %s\n", failure.ExceptionType(), failure.what());
    }
}

} // namespace

int main() {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 40.0).Shape();
    const gp_Dir up(0, 0, 1);
    const double t5 = std::tan(5.0 * pi / 180.0);
    std::printf("OCCT draft probe\n\n");

    run({"A. box sides 5 deg about z = 0", box, verticalFaces(box), 5.0, 0.0, up,
         shrinkingPrism(100, 60, 0, t5, 0, 0, 40)},
        true);
    run({"B. box sides 5 deg about z = 20", box, verticalFaces(box), 5.0, 20.0, up,
         shrinkingPrism(100, 60, 0, t5, 20, 0, 40)});
    run({"C. box sides -5 deg about z = 0", box, verticalFaces(box), -5.0, 0.0, up,
         shrinkingPrism(100, 60, 0, -t5, 0, 0, 40)});
    run({"C2. box sides 5 deg about z = 0, pulled down", box, verticalFaces(box), 5.0, 0.0, gp_Dir(0, 0, -1),
         shrinkingPrism(100, 60, 0, -t5, 0, 0, 40)});
    {
        const double t10 = std::tan(10.0 * pi / 180.0);
        run({"D. side x = 100 at 10 deg", box, {faceNear(box, gp_Pnt(100, 30, 20))}, 10.0, 0.0, up,
             240000.0 - 60.0 * 40.0 * 40.0 * t10 / 2.0});
    }
    {
        const TopoDS_Shape cylinder =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 20.0, 50.0).Shape();
        const double r1 = 20.0 - 50.0 * std::tan(3.0 * pi / 180.0);
        run({"E. cylinder side 3 deg", cylinder, verticalFaces(cylinder), 3.0, 0.0, up,
             pi * 50.0 * (400.0 + 20.0 * r1 + r1 * r1) / 3.0});
    }
    {
        const TopoDS_Shape drilled = BRepAlgoAPI_Cut(
            box, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50, 30, -1), gp_Dir(0, 0, 1)), 10.0, 42.0).Shape()).Shape();
        std::vector<TopoDS_Face> hole;
        for (const TopoDS_Face& face : verticalFaces(drilled)) {
            if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Cylinder) {
                hole.push_back(face);
            }
        }
        const double r1 = 10.0 + 40.0 * std::tan(2.0 * pi / 180.0);
        run({"F. hole 2 deg", drilled, hole, 2.0, 0.0, up,
             240000.0 - pi * 40.0 * (100.0 + 10.0 * r1 + r1 * r1) / 3.0});
    }
    {
        BRepFilletAPI_MakeFillet fillet(box);
        for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(it.Current());
            TopoDS_Vertex a, b;
            TopExp::Vertices(edge, a, b);
            if (std::abs(BRep_Tool::Pnt(a).Z() - BRep_Tool::Pnt(b).Z()) > 1.0) {
                fillet.Add(10.0, edge);
            }
        }
        const TopoDS_Shape rounded = fillet.Shape();
        // Only the four planar sides are added; the rounds are tangent.
        std::vector<TopoDS_Face> planar;
        for (const TopoDS_Face& face : verticalFaces(rounded)) {
            if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane) {
                planar.push_back(face);
            }
        }
        run({"G. rounded box, planar sides 5 deg (rounds follow)", rounded, planar, 5.0, 0.0, up,
             shrinkingPrism(100, 60, 10, t5, 0, 0, 40)});
        run({"G2. rounded box, one side 5 deg (its rounds follow)", rounded,
             {faceNear(rounded, gp_Pnt(100, 30, 20))}, 5.0, 0.0, up, none});
    }
    run({"H. box sides 45 deg (the top would be 20 x -20)", box, verticalFaces(box), 45.0, 0.0, up, none});
    run({"H2. box sides 60 deg", box, verticalFaces(box), 60.0, 0.0, up, none});
    run({"H3. box sides 36.87 deg (top 40 x 0)", box, verticalFaces(box), std::atan(0.75) * 180.0 / pi, 0.0, up,
         none});
    run({"H4. box sides 30 deg (top 53.8 x 13.8)", box, verticalFaces(box), 30.0, 0.0, up,
         shrinkingPrism(100, 60, 0, std::tan(30.0 * pi / 180.0), 0, 0, 40)});
    run({"H5. side x = 100 at 89 deg", box, {faceNear(box, gp_Pnt(100, 30, 20))}, 89.0, 0.0, up, none});
    run({"H6. side x = 100 at 70 deg (reaches past x = 0)", box, {faceNear(box, gp_Pnt(100, 30, 20))}, 70.0, 0.0,
         up, none});
    run({"I. top face (parallel to the neutral plane)", box, {faceNear(box, gp_Pnt(50, 30, 40))}, 5.0, 0.0, up,
         none});
    run({"I2. side x = 100 pulled along +X", box, {faceNear(box, gp_Pnt(100, 30, 20))}, 5.0, 0.0,
         gp_Dir(1, 0, 0), none});
    run({"I3. side x = 100 about a neutral plane above the box (z = 60)", box,
         {faceNear(box, gp_Pnt(100, 30, 20))}, 5.0, 60.0, up,
         // Below the plane the face moves out by (60 - z) tan(a): the box
         // grows by 60 * integral of that over z in [0, 40].
         240000.0 + 60.0 * 1600.0 * std::tan(5.0 * pi / 180.0)});
    run({"J. zero angle", box, verticalFaces(box), 0.0, 0.0, up, 240000.0});
    {
        const TopoDS_Shape cylinder =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 20.0, 50.0).Shape();
        run({"J2. cylinder, zero angle", cylinder, verticalFaces(cylinder), 0.0, 0.0, up, pi * 400.0 * 50.0});
    }
    // Past the degenerate top (36.87 deg) the top would turn inside out.
    for (const double a : {36.5, 36.8, 37.0, 38.0, 40.0, 42.0, 44.0}) {
        char name[64];
        std::snprintf(name, sizeof name, "H7. box sides %.1f deg", a);
        run({name, box, verticalFaces(box), a, 0.0, up,
             shrinkingPrism(100, 60, 0, std::tan(a * pi / 180.0), 0, 0, 40)});
    }
    {
        // A pocket 60 x 30, 25 deep, in the box's top; its four walls
        // drafted 3 deg about the top (z = 40), pulled up: below the
        // neutral plane material is added, so the pocket narrows by
        // (40 - z) tan(a) on each side going down.
        const TopoDS_Shape pocketed =
            BRepAlgoAPI_Cut(box, BRepPrimAPI_MakeBox(gp_Pnt(20, 15, 15), 60.0, 30.0, 26.0).Shape()).Shape();
        std::vector<TopoDS_Face> walls;
        for (const TopoDS_Face& face : verticalFaces(pocketed)) {
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            const gp_Pnt c = props.CentreOfMass();
            if (c.X() > 1 && c.X() < 99 && c.Y() > 1 && c.Y() < 59) {
                walls.push_back(face);
            }
        }
        const double t3 = std::tan(3.0 * pi / 180.0);
        // Pocket section at depth u below the top: (60 - 2 u t)(30 - 2 u t), u in [0, 25].
        const double pocket = integrate(1800.0, -2.0 * t3 * 90.0, 4.0 * t3 * t3, 0.0, 25.0);
        std::printf("   pocket walls found: %zu\n", walls.size());
        run({"K2. pocket walls 3 deg about the top", pocketed, walls, 3.0, 40.0, up, 240000.0 - pocket});
    }
    {
        // The L prism (arms 100 x 30 and 40 x 80, 50 high), all six sides
        // drafted 4 deg about z = 0: the section shrinks with square corners.
        BRepBuilderAPI_MakePolygon polygon;
        const std::pair<double, double> corners[] = {{0, 0}, {100, 0}, {100, 30}, {40, 30}, {40, 80}, {0, 80}};
        for (const auto& [x, y] : corners) {
            polygon.Add(gp_Pnt(x, y, 0.0));
        }
        polygon.Close();
        const TopoDS_Shape ell =
            BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(), gp_Vec(0, 0, 50)).Shape();
        const double t4 = std::tan(4.0 * pi / 180.0);
        // A(s) = (100 - 2s)(30 - 2s) + (40 - 2s)(80 - 2s) - (40 - 2s)(30 - 2s), s = z t.
        const double c0 = 3000.0 + 3200.0 - 1200.0;
        const double c1 = -2.0 * t4 * (130.0 + 120.0 - 70.0);
        const double c2 = 4.0 * t4 * t4;
        run({"L. L prism, six sides 4 deg", ell, verticalFaces(ell), 4.0, 0.0, up, integrate(c0, c1, c2, 0.0, 50.0)});
    }
    {
        // The box with its four top edges rounded r 5: the sides' tangent
        // chains run through the rounds (cylinders along X or Y) into the
        // top, which is parallel to the neutral plane.
        BRepFilletAPI_MakeFillet fillet(box);
        for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(it.Current());
            TopoDS_Vertex a, b;
            TopExp::Vertices(edge, a, b);
            if (BRep_Tool::Pnt(a).Z() > 39.0 && BRep_Tool::Pnt(b).Z() > 39.0) {
                fillet.Add(5.0, edge);
            }
        }
        const TopoDS_Shape topRounded = fillet.Shape();
        std::vector<TopoDS_Face> sides;
        for (TopExp_Explorer it(topRounded, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            BRepAdaptor_Surface surface(face);
            if (surface.GetType() == GeomAbs_Plane && std::abs(surface.Plane().Axis().Direction().Z()) < 1e-12) {
                sides.push_back(face);
            }
        }
        run({"M. top-rounded box, four sides 3 deg", topRounded, sides, 3.0, 0.0, up, none});
        run({"M2. top-rounded box, one side 3 deg", topRounded, {faceNear(topRounded, gp_Pnt(100, 30, 17.5))}, 3.0,
             0.0, up, none});
    }
    return 0;
}
