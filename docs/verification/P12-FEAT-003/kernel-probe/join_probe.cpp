// Kernel probe (evidence, not part of the build): which join type
// BRepOffsetAPI_MakeThickSolid::MakeThickSolidByJoin (OCCT 8.0.1) handles
// reliably, over a table of shapes x {inward, outward} x {arc, intersection},
// against analytic volumes; and whether the kernel's history tells a real
// shell from the silently wrong results shell_probe.cpp found.
//
// For each case: done, BRepCheck validity, solids, the result volume
// against the analytic one (where there is one), and the history check
// BetterCAD would apply: every input face that is not removed is kept
// (not deleted) and generates at least one face of the result.
//
// Built like shell_probe.cpp.
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>

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
using ShapeList = NCollection_List<TopoDS_Shape>;
constexpr double pi = M_PI;
constexpr double none = std::numeric_limits<double>::quiet_NaN();

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
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

TopoDS_Shape prism(const std::vector<std::pair<double, double>>& corners, double height) {
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto& [x, y] : corners) {
        polygon.Add(gp_Pnt(x, y, 0.0));
    }
    polygon.Close();
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(), gp_Vec(0, 0, height)).Shape();
}

struct Case {
    std::string name;
    TopoDS_Shape shape;
    std::vector<gp_Pnt> removed; // centroids of the faces to remove
    double thickness;
    double inwardArc, inwardSharp, outwardArc, outwardSharp; // analytic shell volumes, NaN if unknown
};

void run(const Case& c, bool inward, GeomAbs_JoinType join, double expected) {
    ShapeList removed;
    for (const gp_Pnt& p : c.removed) {
        removed.Append(faceNear(c.shape, p));
    }
    BRepOffsetAPI_MakeThickSolid maker;
    const auto start = std::chrono::steady_clock::now();
    std::string status;
    try {
        maker.MakeThickSolidByJoin(c.shape, removed, inward ? -c.thickness : c.thickness, 1e-7, BRepOffset_Skin,
                                   false, false, join);
    } catch (const Standard_Failure& failure) {
        status = std::string("exception ") + failure.what();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%-44s %-8s %-12s ", c.name.c_str(), inward ? "inward" : "outward",
                join == GeomAbs_Arc ? "arc" : "intersection");
    if (!status.empty() || !maker.IsDone()) {
        std::printf("FAILED %s (error %d), %.3f s\n", status.empty() ? "not done" : status.c_str(),
                    status.empty() ? static_cast<int>(maker.MakeOffset().Error()) : -1, seconds);
        return;
    }
    const TopoDS_Shape& result = maker.Shape();
    const bool valid = BRepCheck_Analyzer(result).IsValid();
    ShapeMap solids;
    TopExp::MapShapes(result, TopAbs_SOLID, solids);
    ShapeMap faces;
    TopExp::MapShapes(result, TopAbs_FACE, faces);
    // The history check: every face not removed is kept and generates a
    // face of the result.
    ShapeMap removedMap;
    for (const TopoDS_Shape& face : removed) {
        removedMap.Add(face);
    }
    int keptWithoutOffset = 0;
    int removedKept = 0;
    for (TopExp_Explorer it(c.shape, TopAbs_FACE); it.More(); it.Next()) {
        if (removedMap.Contains(it.Current())) {
            removedKept += faces.Contains(it.Current()) ? 1 : 0;
            continue;
        }
        bool generates = false;
        for (const TopoDS_Shape& image : maker.Generated(it.Current())) {
            generates = generates || (image.ShapeType() == TopAbs_FACE && faces.Contains(image));
        }
        keptWithoutOffset += (maker.IsDeleted(it.Current()) || !generates) ? 1 : 0;
    }
    const double volume = volumeOf(result);
    std::printf("valid %d solids %d faces %2d history %s volume %.9f", valid ? 1 : 0, solids.Extent(), faces.Extent(),
                keptWithoutOffset == 0 && removedKept == 0 ? "ok " : "BAD", volume);
    if (!std::isnan(expected)) {
        std::printf(" expected %.9f rel %.1e", expected, std::abs(volume - expected) / expected);
    }
    if (keptWithoutOffset != 0 || removedKept != 0) {
        std::printf(" (%d kept faces without an offset, %d removed faces left)", keptWithoutOffset, removedKept);
    }
    std::printf(", %.3f s\n", seconds);
}

} // namespace

int main() {
    std::vector<Case> cases;

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 40.0).Shape();
    cases.push_back({"box 100x60x40, top", box, {gp_Pnt(50, 30, 40)}, 5.0, 82500.0, 82500.0,
                     94000.0 + 3000.0 * pi + 250.0 * pi / 3.0, 110.0 * 70.0 * 45.0 - 240000.0});
    cases.push_back({"box, top and bottom", box, {gp_Pnt(50, 30, 40), gp_Pnt(50, 30, 0)}, 5.0, 60000.0, 60000.0,
                     none, 110.0 * 70.0 * 40.0 - 240000.0});
    cases.push_back({"box, top and front (y = 0)", box, {gp_Pnt(50, 30, 40), gp_Pnt(50, 0, 20)}, 5.0,
                     240000.0 - 90.0 * 55.0 * 35.0, 240000.0 - 90.0 * 55.0 * 35.0, none,
                     110.0 * 65.0 * 45.0 - 240000.0});
    cases.push_back({"box, top, 29 (cavity 42x2x11)", box, {gp_Pnt(50, 30, 40)}, 29.0, 240000.0 - 924.0,
                     240000.0 - 924.0, none, none});
    cases.push_back({"box, top, 30 (cavity closes)", box, {gp_Pnt(50, 30, 40)}, 30.0, none, none, none, none});
    cases.push_back({"box, top, 35 (walls overlap)", box, {gp_Pnt(50, 30, 40)}, 35.0, none, none, none, none});

    const TopoDS_Shape ell = prism({{0, 0}, {100, 0}, {100, 30}, {40, 30}, {40, 80}, {0, 80}}, 50.0);
    {
        const double t = 5.0;
        const double sharpCavity = 90.0 * 20.0 + 30.0 * 70.0 - 30.0 * 20.0;
        const double roundCavity = sharpCavity + t * t * (1.0 - pi / 4.0);
        const double sides = (5000.0 + 360.0 * t + (5.0 * pi / 4.0 - 1.0) * t * t) * 50.0;
        const double bottom = 5000.0 * t + 360.0 * pi * t * t / 4.0 + (5.0 * pi / 4.0 - 1.0) * 2.0 * t * t * t / 3.0;
        // Sharp outward: the L of arms 110 x 40 and 50 x 90, 55 high.
        cases.push_back({"L prism (reflex edge), top", ell, {gp_Pnt(38, 31, 50)}, t, 250000.0 - roundCavity * 45.0,
                         250000.0 - sharpCavity * 45.0, sides + bottom - 250000.0, 6900.0 * 55.0 - 250000.0});
    }

    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 20.0, 50.0).Shape();
    cases.push_back({"cylinder r20 h50, top", cylinder, {gp_Pnt(0, 0, 50)}, 3.0, pi * (20000.0 - 289.0 * 47.0),
                     pi * (20000.0 - 289.0 * 47.0), 7668.0 * pi + 90.0 * pi * pi, pi * (529.0 * 53.0 - 20000.0)});

    const TopoDS_Shape drilled = BRepAlgoAPI_Cut(
        box, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50, 30, -1), gp_Dir(0, 0, 1)), 10.0, 42.0).Shape()).Shape();
    cases.push_back({"box with r10 through hole, top", drilled, {gp_Pnt(50, 30, 40)}, 5.0, 82500.0 + 3875.0 * pi,
                     82500.0 + 3875.0 * pi, none, 106500.0 + 2875.0 * pi});

    const auto roundVerticalEdges = [&](double radius) {
        BRepFilletAPI_MakeFillet fillet(box);
        for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(it.Current());
            TopoDS_Vertex a, b;
            TopExp::Vertices(edge, a, b);
            if (std::abs(BRep_Tool::Pnt(a).Z() - BRep_Tool::Pnt(b).Z()) > 1.0) {
                fillet.Add(radius, edge);
            }
        }
        return fillet.Shape();
    };
    {
        const TopoDS_Shape rounded = roundVerticalEdges(10.0);
        // Sections: rounded rectangles; body r 10, cavity r 5, sharp-outward r 15.
        const double body = (6000.0 - (4.0 - pi) * 100.0) * 40.0;
        const double cavity = (4500.0 - (4.0 - pi) * 25.0) * 35.0;
        const double outer = (7700.0 - (4.0 - pi) * 225.0) * 45.0;
        cases.push_back({"box, vertical edges r10, top", rounded, {gp_Pnt(50, 30, 40)}, 5.0, body - cavity,
                         body - cavity, none, outer - body});
    }
    // Rounds no larger than the wall: inward, their offsets vanish.
    cases.push_back({"box, vertical edges r5 (= wall), top", roundVerticalEdges(5.0), {gp_Pnt(50, 30, 40)}, 5.0,
                     none, none, none, none});
    cases.push_back({"box, vertical edges r3 (< wall), top", roundVerticalEdges(3.0), {gp_Pnt(50, 30, 40)}, 5.0,
                     none, none, none, none});

    {
        // A revolved stepped shaft: r 30 for z in [0, 20], r 15 for z in [20, 60].
        // Removing the small end: inward, the cavity is r 10 from z = 25 up and
        // r 25 from z = 5 to 20, joined... not analytic with the reflex circle:
        // validity and history only.
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& [x, z] :
             std::vector<std::pair<double, double>>{{0, 0}, {30, 0}, {30, 20}, {15, 20}, {15, 60}, {0, 60}}) {
            polygon.Add(gp_Pnt(x, 0.0, z));
        }
        polygon.Close();
        const TopoDS_Face section = BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
        const TopoDS_Shape shaft =
            BRepPrimAPI_MakeRevol(section, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 2.0 * pi).Shape();
        // Sharp: inward the cavity is r 27 for z in [3, 17] and r 12 above;
        // outward the outside is r 33 for z in [-3, 23] and r 18 above.
        cases.push_back({"stepped shaft (revolved), small end", shaft, {gp_Pnt(0, 0, 60)}, 3.0, none, 10602.0 * pi,
                         none, 13302.0 * pi});
    }
    {
        // Local over-thickness: two 40 x 40 blocks joined by a bridge 8 wide,
        // 30 high, 5 mm walls. The bridge is narrower than two walls, so it
        // stays solid; each block gets a 30 x 30 x 25 cavity (sharp).
        const TopoDS_Shape dumbbell = prism(
            {{0, 0}, {40, 0}, {40, 16}, {60, 16}, {60, 0}, {100, 0}, {100, 40}, {60, 40}, {60, 24}, {40, 24}, {40, 40},
             {0, 40}},
            30.0);
        const double body = (3200.0 + 160.0) * 30.0;
        cases.push_back({"dumbbell, bridge 8 < 2 walls, top", dumbbell, {gp_Pnt(50, 20, 30)}, 5.0, none,
                         body - 2.0 * 900.0 * 25.0, none, none});
        // A U channel whose one arm (4 wide) is thinner than a wall pair; its
        // top is one U-shaped face (centroid (25.94, 13.24)). The side of the
        // thin arm is removed in a second case.
        const TopoDS_Shape channel =
            prism({{0, 0}, {60, 0}, {60, 40}, {56, 40}, {56, 10}, {10, 10}, {10, 40}, {0, 40}}, 50.0);
        cases.push_back({"U channel, arm 4 < 2 walls, top", channel, {gp_Pnt(25.94, 13.24, 50)}, 3.0, none, none,
                         none, none});
        cases.push_back({"U channel, arm 4 < 2 walls, arm inner side", channel, {gp_Pnt(56, 25, 25)}, 3.0, none,
                         none, none, none});
    }

    std::printf("OCCT shell join probe\n\n");
    for (const Case& c : cases) {
        run(c, true, GeomAbs_Arc, c.inwardArc);
        run(c, true, GeomAbs_Intersection, c.inwardSharp);
        run(c, false, GeomAbs_Arc, c.outwardArc);
        run(c, false, GeomAbs_Intersection, c.outwardSharp);
    }
    return 0;
}
